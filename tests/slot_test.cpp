#include <type_traits>
using std::remove_reference_t;

#include <functional>
using std::function;

#include <optional>
using std::optional;
using std::nullopt;

#include <vector>
using std::vector;

#include <utility>
using std::pair;

#include <iostream>
using std::cout;
using std::endl;

#include "single_thread_executor.h"
using executor = testing::single_thread_executor;

#include "gunit.h"

#include "a_sinq.h"
using a_sinq::loop;
using a_sinq::slot;
using a_sinq::result;

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
    function<void(function<void(optional<int>)>)> numerator(executor& ex, int from, int to)
    {
        return [&ex, i = from, to = to](function<void(optional<int>)> callback) mutable {
            ex.schedule([callback = move(callback), v = i < to ? optional<int>(i++) : nullopt]{
                callback(v);
            });		
        };
    };

    template<typename ITER>
    function<void(function<void(optional<remove_reference_t<decltype(*ITER())>>)>)> list_iterator(ITER begin, ITER end)
    {
        return[i = begin, end = end](function<void(optional<remove_reference_t<decltype(*ITER())>>)> callback) mutable {
            if (i != end) {
                callback(optional<remove_reference_t<decltype(*i)>>(*i));
                ++i;
            } else {
                callback(nullopt);
            }
        };
    };

    struct node {
        int payload;
        vector<node> subnodes;
    };

    void node_iterator(node& current, slot<optional<int>>::provider sink, function<void()> cont) {
        loop by_subnodes([=, &current, i = -1](auto next) mutable {
            if (++i >= current.subnodes.size()) {
                cont();
            } else {
                sink.await([&, next] {
                    sink(current.subnodes[i].payload);
                    node_iterator(current.subnodes[i], sink, next);
                });
            }
        });
    }

    function<void(function<void(optional<int>)>)> tree_iterator(node& root)
    {
        slot<optional<int>> result;
        result.get_provider().await([&root, r = result.get_provider()] {
            r(root.payload);
            node_iterator(root, r, [r]() mutable {
                loop after_end([r](auto next) mutable {
                    r.await([&, next]() mutable {
                        r(nullopt);
                        next();
                    });
                });
            });
        });
        return result;
    }

    function<void(function<void(optional<pair<optional<int>, optional<int>>>)>)> join(
        function<void(function<void(optional<int>)>)> list_a,
        function<void(function<void(optional<int>)>)> list_b)
    {
        slot<optional<pair<optional<int>, optional<int>>>> result;
        loop zipping([list_a = move(list_a), list_b = move(list_b), sink = result.get_provider()](auto next) mutable {
            sink.await([&, next] {
                a_sinq::result<pair<optional<int>, optional<int>>> combined([&, next](auto combined) mutable {
                    sink(combined.first || combined.second ? optional(move(combined)) : nullopt);
                    next();
                });
                list_a(combined.setter(combined->first));
                list_b(combined.setter(combined->second));
            });
        });
        return result;
    }

    TEST(LAsync, SyncSlotTest)
    {
        executor ex;
        node root
            {1, {
                {11, {
                    {111},
                    {112}
                }},
                {12}
            }};
        vector<pair<optional<int>, optional<int>>> expected{ {1, 1}, {2, 11}, {3, 111}, {4, 112}, {5, 12}, {6, nullopt} };
        loop test_loop([
            source_stream = join(numerator(ex, 1, 7), tree_iterator(root)),
            expected_stream = list_iterator(expected.begin(), expected.end())
        ](auto next) {
            source_stream([&, next](auto data) {
                if (!data) return;
                expected_stream([&, next](auto expected_data) {
                    ASSERT_EQ(data, expected_data);
                    next();
                });
            });
        });
        ex.execute();
    }
}
