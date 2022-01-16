# A-sinq
A Lightweight C++11 library for asynchronous programming


### Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for details.

### License

Apache 2.0; see [`LICENSE`](LICENSE) for details.

### Disclaimer

This project is not an official Google project. It is not supported by
Google and Google specifically disclaims all warranties as to its quality,
merchantability, or fitness for a particular purpose.

## Example

Suppose we need to calc the total size of all files in the given tree of subdirectories using the provided file system API:

```C++
template <typename T>
struct async_stream {
    virtual ~async_stream() = default;
    virtual void get_next_item(function<void(unique_ptr<T>)> callback) = 0;
};

struct async_file {
    virtual ~async_file() = default;
    virtual void get_size(function<void(int)> callback) const = 0;
};

struct async_dir {
    virtual ~async_dir() = default;
    virtual unique_ptr<async_stream<async_file>> get_files() const = 0;
    virtual unique_ptr<async_stream<async_dir>> get_dirs() const = 0;
};
```

Our goal is to traverse the lists of files and directories, acquire file sizes and calculate the total size:

```C++
void calc_tree_size_async(const async_dir& root, function<void(int)> callback);
```

This API is async, that allows us to speed-up our tasks because our thread doesn't have to wait on `get_next_item`/`get size` blocking calls, and even more, it allows us to traverse many subdirectories in parallel, but on the other hand our async code will be very tricky and cumbersome:
- Our data structures have to preserve `async_stream` instances across asynchronous iterations of `async_stream::get_next_item` calls.
- We have to support the nested-recursive or parallel-co-existing iteration contexts with data and results.
- We need to organize some reactive result delivery and callbacks notification mechanisms.
- `std::unique_ptr`s are not copy-constructible, so `std::function`s, should we elect to use them, can't store these pointers in their capture blocks.

Overall, is it hard to write such code?
You can stop reading here and try to make your own solution first.

This is my solution using `a-sinq`:

```C++
#include "a-sinq.h"
using a_sinq::loop;
using a_sinq::result;
using a_sinq::unique;

void calc_tree_size_async(const async_dir& root, result<int> result) {
    loop dirs([=, stream = unique(root.get_dirs())](auto next) mutable {
        stream->get_next_item([&, next](auto dir) {
            if (!dir) return;
            calc_tree_size_async(*dir, result);
            next();
        });
    });
    loop files([=, stream = unique(root.get_files())](auto next) mutable {
        stream->get_next_item([&, next](auto file) {
            if (!file) return;
            file->get_size([=](int size) mutable {
                *result += size;
            });
            next();
        });
    });
}

void calc_tree_size_async(const async_dir& root, function<void(int)> callback) {
    calc_tree_size_async(root, result<int>(callback));
}
```

This solution:
- has about the same size as in synchronous case,
- has same structure and same complexity,
- and even more, it performs scan in parallel,
- and it is protected against stack overflow in the case the if `async_stream::get_next_item` calls its callbacks synchronously.

Detailed comparison of sync and async code can be found [TBD](TBD)

## How it works

The entire library consists of just four primitives:
- `a_sinq::unique<T>`
- `a_sinq::result<T>`
- `a_sinq::loop`
- `a_sinq::slot<T>`

### `a_sinq::unique`

C++ language designers should have supported move-only lambdas capturing move-only data types. But they didn't. That's what `unique` is for: it wraps data type and lies to the compiler that this type is now copy-constructible, but it fails on assert on copy attempts. Of course, this wrapper should be used only in lambdas that are move-only by design. Luckily `a_sinq::loop` and `a_sinq::result`  guarantee that their lambdas will never be copied.

So it is a transparent wrapper for any type. It supports move semantics, disallows assignments and terminates programs on attempts to copy data. It's useful when we need to capture move-only objects (like `std::unique_ptr`) in lambdas.

In the above example it is used to store file and dir streams between `get_next_item` iterations.

### `a_sinq::result<T>`

It's a shared_ptr to a memory block, that holds data of a given type along with a callback that accepts this type as parameter.
* You can freely pass this object by value and store it in any levels of your processing lambdas.
* You can access and modify this data.
* At the moment it is no longer referenced, it calls its callback with its data.

You can think of it as the less limiting generalization of `promise_all` pattern in other languages.

It can be used to combine data from different branches of asynchronous processes.
Tree of `a_sinq::result`-s provides reactive data and control transfers for processes having subprocesses.

It is useful to organize the parallel loops and combine the parallel results of different processes.

### `a_sinq::loop`

It's a workhorse of this library. It organizes the asynchronous iterative processes.
It accepts a lambda that captures the data that should be preserved across iterations; the lambda body becomes the loop body.
- First the `a_sinq::loop` moves its lambda to a heap-allocated shared block. This guarantees that captured objects will never be copied.
- Then it calls this lambda passing to it a shared pointer to this block as a parameter (who said Y-combinator?).

From the data lifetime perspective: In the loop body we have the direct access to the context data and to the `next` object, that upholds this context data.
From the control flow perspective: The first call to the passed lambda is performed synchronously at the `loop` creation. The `next` parameter not only controls
context data lifetime, it also can be called to perform the next loop iteration.

Overal, `a_sinq::loop` is a `std::function<void()>` and also it's a shared pointer to lambda and all its captures.\
All `auto next` parameters in the above example is of type  `a_sinq::loop`. 

Our loop body lambda can use its `next` parameter in four ways:
- Ignore it; this breaks the loop and destroys the context.
- Or pass it to some function, that expects `std::function<void()>` to be called later; this prolongs lifetime of the context data and allows asynchronous iterations.
- Or capture it in some callback, that expects data and call later from that callback as shown in the above example, this is also produces asynchronous iterations.
- Or call it synchronously or pass to a function that will call it synchronously; this also initiates the new iteration but in slightly different manner: it sets a flag, that will make `a_sinq::loop` to restart the lambda immediately after it returned, performing iteration without stack overflow. BTW, it might be this case in the above example, if `stream->get_next_item` will call its callback synchrously.

### `a_sinq::slot<T>`

Async data processing often uses the concept of data providers and data consumers.
Generalized data consumer is a callback that accepts data: `function<void(T)> callback`. With the help of `a_sinq` it is very easy to write data consumers that request data sequentially (`a_sinq::loop`), and/or in parallel (`a_sinq::result`). Consumer can have own callbacks and call another consumers, thus consumers are combinable.

The providers are a little bit more trickiy.
- Generalized provider accepts a request for data with callback: `function<void(function<void(T)> callback)> provider`.
- It never provides data until requested.
- Each data request can have different callback.
- In the basic case one request assumes one response.
- The `async_stream::get_next_item` and `async_file::get_size` are the two examples of data providers.

The `a_sinq::slot` allows to make data providers:
1. Create instance of `a_sinq::slot<T>` and give it to consumers. It is a shared_ptr to the real object. It's also a `function<void(function<void(T)> callback)> provider`. It can be called by any consumer.
2. Before the instance of `a_sinq::slot<T>` is given to consumers, you should take and store your own "provider" part of this slot with `auto prov = get_provider()`. It is also a `shared_ptr`.
3. When you finished your provider initialization and are ready to serve the requests, call `prov.await([]{...});`, this call will store its lambda till the moment, the consumer will either call the slot for data or destroy it.
   - If it is destroyed, slot simply destoys the passed lambda ending the operation and freeing all resources,
   - If data is requested, this lambda will be called, and you'll need to prepare data sync or async, doesn't matter, and call your `prov()` with your data. Yes it is also a `function(T)`

#### Example:

This async data provider takes two other async data providers that provide streams of `optional<A>` and `optional<B>` (where `nullopt` signals the end of stream), and returns their inner-join in the form of the stream of `optional<pair<A, B>>`
```C++
template<typename T> using listener = std::function<void(optional<T>)>;
template<typename T> using stream = std::function<listener(T)>;

template<typename A, typename B>
stream<pair<A, B>> inner_join(
    stream<A> a,
    stream<B> b)
{
    slot<optional<pair<A, B>>> result;  // [1]
    loop zipping([
        a = move(a),
        b = move(b),
        sink = result.get_provider()  // [2]
    ](auto next) mutable {
        sink.await([&, next] {  // [3]
            a_sinq::result<pair<optional<A>, optional<B>>> combined([&, next](auto r) mutable {  // [4]
                sink(r.first && r.second  // [5]
                    ? optional(pair{move(*r.first), move(*r.second)})
                    : nullopt);
                next();  // [6]
            });
            a(combined.setter(combined->first));  // [7]
            b(combined.setter(combined->second));  // [8]
        });
    });
    return result;  // [9]
}
```
Where
- We create \[1] and return \[9] our data provider `slot`.
- We take and hold our counterpart \[2]
- We register that we are ready to serve the next request \[3] When the consumer deletes our slot object, this lambda will be deleted. This also deletes `seq_a` and `seq_b`. 
- On the incoming data request from consumer we create the `combined` `result` to accumulate the results of two parallel outgoing requests, that could be received in any order and possibly asynchronously. \[4]
- Then we perform two parallel requests on `a` \[7] and `b` \[8].
- After two results are done fetching, we notify our consumer by calling `sink` at \[5]
- And by calling `next` \[6] we restart our `zipping` loop, which calls the `sink.await` and make us ready to receive another request.
- Our `loop` will continue working until our consumer deletes our `slot` object, and this automatically deletes sync lambda \[3], next object and loop variables.

Slots are useful for building the chained data providers and for creating state machines, because `await` in the same `slot` can be called with different lambdas.

See more in `examples/slot_example.cpp`.
## Structure
- `include/a_sinq.h` - single header library itself,
- `examples/*` - demonstration on how to use a-sinq primitives,
