#include <type_traits>
#include <functional>
#include <optional>
#include <vector>
#include <utility>
#include <iostream>

using std::remove_reference_t;
using std::function;
using std::optional;
using std::nullopt;
using std::vector;
using std::pair;
using std::move;
using std::cout;
using std::endl;

#include "single_thread_executor.h"
using executor = testing::single_thread_executor;

#include "gunit.h"

#include "a_sinq.h"
using a_sinq::loop;
using a_sinq::slot;
using a_sinq::result;

// Let's make `std::pair` and `std::optional` printable
namespace std
{
    template<typename T>
    ostream& operator<< (ostream& out, const optional<T>& val)
    {
        return val
            ? out << *val
            : out << "<nullptr>";
    }

    template<typename A, typename B>
    ostream& operator<< (ostream& out, const pair<A, B>& val)
    {
        return out << "{" << val.first << ", " << val.second << "}";
    }
}

namespace
{
    // In this example data is organized in (possibly infinite) streams of items
    // requested by consumer from provider.
    //
    // Each stream element is requested in the form of `optional<T>`,
    // where `nullopt` signals the end of stream.
    // 
    // To avoid UB, let's state, that after the end all stream providers 
    // will return the infinite sequence of `nullopt`s.
    // 
    // So a consumer asks a provider for another stream item and passes
    // callback, that expects `optional<T>`:
    template<typename T>
    using stream_callback = function<void(optional<T>)>;

    // The provider function takes callback as parameter:
    template<typename T>
    using stream = function<void(stream_callback<T>)>;

    // This is an example of async stream of numbers in a range `from`...`to`:
    stream<int> range_stream(executor& ex, int from, int to)
    {
        return [&ex, i = from, to = to](stream_callback<int> callback) mutable {
            ex.schedule([
                callback = move(callback),
                v = i < to ? optional<int>(i++) : nullopt
            ]{ callback(v); });		
        };
    };

    // This is an example of a stream of data from any iterable container.
    // It responds synchronously to demonstrate, that a_sinq supports mixing of sync and async data.
    // The iterated container should outlive this `iterable_stream`.
    template<typename ITER, typename T = remove_reference_t<decltype(*ITER())>>
    stream<T> iterator_stream(ITER begin, ITER end)
    {
        return[i = begin, end = end](stream_callback<T> callback) mutable {
            if (i != end) {
                callback(optional<T>(*i));
                ++i;
            } else {
                callback(nullopt);
            }
        };
    };

    // This is an example of a stream, that joins two streams
    // and returns pairs of elements from both:
    template<typename A, typename B>
    stream<pair<A, B>> inner_join(stream<A> a, stream<B> b)
    {
        slot<optional<pair<A, B>>> stream;
        loop zipping([
            a = move(a),
            b = move(b),
            sink = stream.get_provider()
        ](auto next) mutable {
            sink.await([&, next] {
                result<pair<optional<A>, optional<B>>> expected([&, next](auto r){
                    sink(r.first && r.second
                        ? optional(pair{move(*r.first), move(*r.second)})
                        : nullopt);
                    next();
                });
                a(expected.setter(expected->first));  // these requests...
                b(expected.setter(expected->second)); // ...performed in parallel
            });
        });
        return stream;
    }

    // Makes an infinite sequence of `nullopt`s
    // So, streams can be infinite
    template<typename T>
    function<void()> nullopt_stream(T sink) {
        return [=] {
            loop infinite([=](auto next) {
                sink.await([&, next] {
                    sink(nullopt);
                    next();
                });
            });
        };
    }

    // The next example demonstrates a recursive generator that traverses a tree and yields each tree node.
    // This is a interruptable/resumable process with subprocessess.
    // This example is often used to demonstrate the power of `yield return`, `async`, `await`
    // in languages having these constructions.
    // a_sinq does so with about the same complexity not using `await/yield` constructs.
    struct node {
        int payload;
        vector<node> subnodes;

        void scan(slot<optional<int>>::provider sink, function<void()> after_subtree) {
            sink.await([=] {
                sink(payload);
                loop by_subnodes([=, i = -1](auto next) mutable {
                    if (++i < subnodes.size())
                        subnodes[i].scan(sink, next);
                    else
                        after_subtree();
                });
            });
        }
    };

    stream<int> tree_stream(node& root) {
        slot<optional<int>> result;
        root.scan(result.get_provider(), nullopt_stream(result.get_provider()));
        return result;
    }

    TEST(LAsync, SlotExample)
    {
        executor ex;

        // Let's scan tree nodes and inner join their payloads with numeric range.
        // And then compare this results with predefined sequence:

        node root
            {1, {
                {11, {
                    {111},
                    {112}
                }},
                {12}
            }};
        vector<pair<int, int>> expected{ {1, 1}, {2, 11}, {3, 111}, {4, 112}, {5, 12} };

        loop test_loop([
            stream = inner_join(
                inner_join(
                    range_stream(ex, 1, 100500),
                    tree_stream(root)),
                iterator_stream(expected.begin(), expected.end()))
        ](auto next) {
            stream([&, next](auto item) {
                if (!item) return;
                ASSERT_EQ(item->first, item->second);
                next();
            });
        });
        ex.execute();
    }
}
