/// This is a variation of loop_example.cpp
/// simplified by using `a_sinq::result`.

#include <functional>
using std::function;

#include <optional>
using std::optional;
using std::nullopt;

#include <vector>
using std::vector;
using std::move;

#include "single_thread_executor.h"
using executor = testing::single_thread_executor;

#include "a_sinq.h"
using a_sinq::loop;
using a_sinq::result;

#include "gunit.h"

namespace
{
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

    void accumulate(
        async_data_stream stream,
        function<void(vector<int>)> callback)
    {
        a_sinq::loop for_stream([
            stream = move(stream),
            result = result<vector<int>>(callback)  // [1]
        ](auto next) mutable {
            stream.get_data([&, next](auto data) {
                if (!data) return;                  // [2]
                result->push_back(*data);
                next();
            });
        });
    }

    // -- Explanation ---------------

    /// `a_sinq::result` combines the callback and the data this
    /// callback expects, holds it i shared heap block and notifies
    /// callback when this block is no more referenced.
    /// It prevents dangled, not called callbacks.
    /// It simplifies code.
    /// It is useful for the complex recursive asynchronous
    /// operations.
    /// In line [1] we combine data and callback.
    /// In line [2] we just return from lambda without calling `next()`.
    /// This terminates loop, destroys all its context and automatically
    /// calls the callback.

    // -- Test ---------------------

    TEST(LAsync, ResultExample)
    {
        executor ex;
        accumulate(async_data_stream(ex), [](std::vector<int> data) {
            ASSERT_EQ(data.size(), size_t(3));
            for (int i = 0; i < data.size(); ++i)
            {
                ASSERT_EQ(data[i], i);
            }
        });
        ex.execute();
    }
}
