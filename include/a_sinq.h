/*
Copyright 2018 Google LLC
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    https://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#ifndef _A_SINQ_H_
#define _A_SINQ_H_
#include <memory>
#include <functional>
#include <cassert>
namespace a_sinq {
    class loop {
        std::shared_ptr<
            std::pair<
                std::function<void(const loop&)>,
                bool>> body;
    public:
        loop(std::function<void(std::function<void()>)> body) {
            this->body = std::make_shared<
                std::pair<
                    std::function<void(const loop&)>,
                    bool>>(std::move(body), false);
            operator()();
        }
        void operator() () {
            while ((body->second = !body->second))
                body->first(*this);
        }
    };
    template<typename T>
    class result {
        struct data_t {
            T data;
            std::function<void(T)> callback;
            data_t(T data, std::function<void(T)> callback)
                : data(std::move(data))
                , callback(std::move(callback)) {}
            ~data_t() { callback(std::move(data)); }
        };
        std::shared_ptr<data_t> ptr;
    public:
        result(std::function<void(T)> callback, T initial_value = T())
            : ptr(std::make_shared<data_t>(
                std::move(initial_value),
                std::move(callback))) {}
        T& operator* () { return ptr->data; }
        T* operator-> () { return &ptr->data; }
        template<typename X>
        function<void(X)> setter(X& dst) {
            return [dst = &dst, holder = ptr](X value) { *dst = move(value); };
        }
    };
    template<typename T>
    class unique {
        T data;
    public:
        unique(T&& data) : data(std::move(data)) {}
        unique(unique<T>&& src) : data(std::move(src.data)) {}
        unique(const unique<T>& data) { assert(false); }
        operator T& () { return data; }
        operator const T& () const { return data; }
        T& operator-> () { return data; }
        void operator= (const unique<T>&) = delete;
    };
    template <typename T>
    class slot {
        struct data {
            std::function<void()> who_awaits_request;
            std::function<void(T)> who_awaits_data;
        };
        std::shared_ptr<data> ptr;
    public:
        class provider {
            std::weak_ptr<data> ptr;
        public:
            provider(std::weak_ptr<data> ptr) : ptr(std::move(ptr)) {}
            void await(std::function<void()> request_listener) const {
                if (auto p = ptr.lock()) {
                    assert(!p->who_awaits_request);
                    if (p->who_awaits_data)
                        request_listener();
                    else
                        p->who_awaits_request = move(request_listener);
                }
            }
            void operator() (T value) const {
                if (auto p = ptr.lock()) {
                    assert(p->who_awaits_data);
                    std::function<void(T)> temp;
                    std::swap(temp, p->who_awaits_data);
                    temp(std::move(value));
                }
            }
        };
        slot() : ptr(std::make_shared<data>()) {}
        void operator() (std::function<void(T)> data_listener) {
            assert(!ptr->who_awaits_data);
            ptr->who_awaits_data = std::move(data_listener);
            if (ptr->who_awaits_request) {
                std::function<void()> temp;
                std::swap(temp, ptr->who_awaits_request);
                temp();
            }
        }
        provider get_provider() { return provider{ std::weak_ptr<data>(ptr) }; }
    };
}
#endif  // _A_SINQ_H_
