/// The simplest example of lightweight async loop
/// Suppose we have a data source, that provides data asynchronously using callback
/// For testing purpose our data source will use single_thread executor,
/// while real life data source could use async io and/or multiple threads.

// -- Problem ---------------

#include "single_thread_executor.h"
using executor = testing::single_thread_executor;

#include <functional>
using std::function;

#include <optional>
using std::optional;
using std::nullopt;

struct async_data_stream
{
    int i = 0;
    executor& ex;
    async_data_stream(executor& ex)
        : ex(ex)
    {}

    // Our testing data stream returns three numbers and end-of-stream.
    void get_data(function<void(optional<int>)> callback)
    {
        ex.schedule([callback = move(callback), v = i++] {
            callback(v < 3 ? optional<int>(v) : nullopt);
        });
    }
};

// Our goal is to accumulate this stream in the std::vector<int>
#include <vector>
using std::vector;
using std::move;

void accumulate(async_data_stream stream, function<void(vector<int>)> callback);

// -- Difficulties -----------

/// The `stream` and `callback` will be destroyed
/// in the end of `accumulate` function. But when we
/// call `stream.get_data` for the first time, it
/// might call our callback after `accumulate`
/// ended. So we'll need to store them somewhere in
/// the heap. Also we need to store resultimg
/// vector.
///
/// This iteration context could be preserved across
/// iterations. Its lifetime should be connected to
/// the lifetime of callback we'll pass to
/// `stream.get_data`. Since this callback is
/// std::function, it is copyable. We need to share
/// context across callback instances.
///
/// Callback should have access not only to the
/// context data, but also to itself to be able to
/// make another `stream.get_data` calls. Since
/// callback (its multiple instances) uphold context
/// data, the context itself can't uphold the
/// callbacks.
///
/// Functions like `stream.get_data` can call their
/// callbacks synchronously. And if callbacks start
/// next iteration by immediately calling
/// `stream.get_data` the loop iterations will be
/// transformed into recursion that might quickly
/// deplete the stack. Thus we need a mechanism that
/// detects such synchronus calls and prevents stack
/// overflow.

// -- Solution ---------------

#include "a_sinq.h"
using a_sinq::loop;

void accumulate(
    async_data_stream stream,
    function<void(vector<int>)> callback)
{
    loop for_stream([                           // [1]
        stream = move(stream),                  // [2]
        callback = move(callback),
        result = vector<int>()                  // [3]
    ](auto next) mutable {                      // [4]
        stream.get_data([&, next](auto data) {  // [5]
            if (data) {
                result.push_back(*data);
                next();                         // [6]
            } else {
                callback(move(result));         // [7]
            }
        });
    });
}

// -- Explanation ---------------

/// `a_sinq::loop` is a data type, that performs
/// all its actions in constructor. In line [1] we
/// create a local variable of its type.
/// 
/// Instance of `a_sinq::loop` is a shared pointer
/// to the context data incapsulated in std::function.
/// In line [2] we move our parameters in that context,
/// in line [3] we store there our result.
/// 
/// `loop` accepts lambda and moves it to the heap-
/// allocated shared block. It guarantees that captured
/// objects will never be copied.
/// 
/// Then it calls our lambda passing itself as parameter
/// (yes, `auto next` in line [4] is also `a_sinq::loop`
/// instance.
/// 
/// From the data lifetime perspective in lambda body we
/// have direct access to the context data and the `next`
/// object, that upholds this context data.
/// 
/// From the control flow perspective, the first call of
/// lambda is performed synchronously at the `loop`
/// creation. The `next` paramter not only controls
/// context data lifetime, it also it can be called to
/// perform the next loop iteration.
/// 
/// Our lambda can use its `next` parameter four ways:
/// - Ignore it; this breaks the loop and destroys
///   context.
/// - Or pass it to some function, that expects
///   std::function<void()> to be called later; this
///   prolongs lifetime of the context data and allows
///   asynchronous iterations.
/// - Or capture it in some callback, that expects data
///   and call later from that callback as shown in
///   line [5], this is also produces asynchronous
///   iterations.
/// - Call it synchronously or pass to a function that
///   will call it synchronously; this sets a flag, that
///   will restart the lambda immediately after it
///   returned, performing iteration without stack
///   overflow. BTW, in line [5] there might be this
///   case, if `stream.get_data` will call its callback
///   synchrously.
/// 
/// Line [5] demonstrates the way we pass context to the
/// nested callbacks: all context data is passed by
/// reference, but `next` is passed by value to uphold
/// the context and optionally make it possible to start
/// a new iteration.
/// 
/// Line [6] starts new iteration. It works correctly in
/// both cases:
/// - If it is called synchronously in scope of lambda, it
///   just sets flag, alowing `loop` to restart lambda as
///   soon as it returns.
/// - If it call asynchronously, it calls lambda right away.
/// It is good to have this `next()` call the last statement
/// of lambda, because of sync/async variation of its
/// behavior.
/// 
/// Line [7] demonstrates the loop termination. It teminates
/// itself by not calling `next()`. All we need to do is to
/// call the outer callback, if it hasn't been already
/// automated by using `a_sinq::result`.

// -- Test ----------------------

#include "gunit.h"

TEST(Asinq, LoopExample)
{
    executor ex;
    accumulate(async_data_stream(ex), [](std::vector<int> data) {
        ASSERT_EQ(data.size(), size_t(3));
        for (int i = 0; i < data.size(); ++i)
        {
            ASSERT_EQ(data[i], i);
        }
    });
}
