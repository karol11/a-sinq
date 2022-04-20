#include <functional>
#include <unordered_map>
#include "gunit.h"

namespace experiment {

    // LRU layout:   tail  NOMINATED  nom  ADDED  inlet REUSED  head  |  PINNED
    // Newly created node is added at the inlet position. It travels to nom and marked as NOMINATED.
    // If nominated item is not accessed, it travels to `tail` and evicts.
    // If nominated item is accessed, it jumps to head.
    // tail..nom range takes about 1/4 of cache size. It holds nominated items. Only these items are moved in cache on access
    // nom..inlet range holds newly cached items, it is 1/16 of cache, it prevents newly added items that was intensively acessed right after adding to be treated as very hot.
    // inlet..head range holds intensively used work set. Items that accessed in regular basis.
    // On heavy throttling only tail..inlet range is trashed.
    // Some items can be marked as pinned, they are extracted from the queue. On unpin, thay are moved to inlet.
    // Some other items can forcefully be marked as FORCE_COLD. They will be evicted after staying in queue regardless if they were accessed or not. This used to retain eviction order.

    struct LruNodeBase {
        enum class State {
            NOMINATED = 0,
            ADDED = 1,     // todo
            REUSED = 2,
            DETACHED = 3,
        };
        LruNodeBase() { next = prev = this; }
        LruNodeBase* next;
        LruNodeBase* prev;
        State state = State::DETACHED;
    };
    template<typename K, typename V>
    struct LruNode : LruNodeBase {
        typename std::unordered_map<K, LruNode<K, V>>::iterator in_map;
        V value;
        LruNode(V value) : value(std::move(value)) {}
    };
    template<typename K, typename V>
    struct LruCache {
        template<typename A, typename B>
        friend void check(LruCache<A, B>&, std::initializer_list<const B>, std::initializer_list<const B>, std::initializer_list<const B>);
    public:
        LruCache(int size,
            std::function<V(const K&)> on_create = nullptr,
            std::function<void(const K&, V&)> on_evict = nullptr,
            int nominated_size = 0,
            int added_size = 0
        )
            : cache_limit(size)
            , nominated_limit(nominated_size ? nominated_size : (size / 2))
            , added_limit(added_size ? added_size : (nominated_limit + size / 4))
            , on_create(std::move(on_create))
            , on_evict(std::move(on_evict))
        {
            inlet = nomination = &dummy;
            assert(nominated_limit > 0);
            assert(added_limit > nominated_limit);
            assert(cache_limit > added_limit);
        }
        LruCache(const LruCache& src) = delete;

        V& operator[] (const K& key) {
            if (auto i = map.find(key); i != map.end()) {
                use_(i->second);
                return static_cast<LruNode<K, V>&>(i->second).value;
            }
            auto r = map.insert({ key, LruNode<K, V>(on_create ? on_create(key) : V(key)) }).first;
            static_cast<LruNode<K, V>&>(r->second).in_map = r;
            add_(r->second);
            return r->second.value;
        }
    private:
        LruNodeBase dummy;  // its prev is head, its next is tail
        LruNodeBase* inlet;
        LruNodeBase* nomination;
        int cache_limit, nominated_limit, added_limit;
        int cache_size = 0;
        std::unordered_map<K, LruNode<K, V>> map;
        std::function<V(const K&)> on_create;
        std::function<void(const K&, V&)> on_evict;

        void remove_(LruNodeBase* n) {
            n->next->prev = n->prev;
            n->prev->next = n->next;
        }
        void insert_before_(LruNodeBase* at, LruNodeBase* n) {
            n->next = at;
            n->prev = at->prev;
            n->prev->next = n;
            at->prev = n;
        }
        void make_detached_(LruNodeBase* n) {
            n->next = n->prev = n;
            n->state = LruNode::State::DETACHED;
        }
        void use_(LruNodeBase& n) {
            if (n.state != LruNodeBase::State::NOMINATED)
                return;
            remove_(&n);
            insert_before_(&dummy, &n);
            n.state = LruNodeBase::State::REUSED;
            if (nomination->state == LruNodeBase::State::ADDED) {
                nomination->state = LruNodeBase::State::NOMINATED;
                nomination = nomination->next;
                if (inlet->state == LruNodeBase::State::REUSED) {
                    inlet->state = LruNodeBase::State::ADDED;
                    inlet = inlet->next;
                }
            }
        }
        void add_(LruNodeBase& n) {
            if (n.state != LruNodeBase::State::DETACHED)
                return;
            n.state = LruNodeBase::State::ADDED;
            if (cache_size >= cache_limit) {
                auto node_to_remove = static_cast<LruNode<K, V>*>(dummy.next);
                remove_(node_to_remove);
                V val_to_remove = std::move(node_to_remove->value);
                K key_to_remove = std::move(node_to_remove->in_map->first);
                map.erase(node_to_remove->in_map);
                insert_before_(inlet, &n);
                nomination->state = LruNodeBase::State::NOMINATED;
                nomination = nomination->next;
                if (on_evict)
                    on_evict(key_to_remove, val_to_remove);
            } else {
                if (cache_size < nominated_limit) {
                    n.state = LruNodeBase::State::NOMINATED;
                } else if (cache_size == nominated_limit) {
                    nomination = &n;
                } else {
                    inlet = inlet->prev;
                    if (nomination == inlet)
                        nomination = &n;
                    inlet->state = LruNodeBase::State::REUSED;
                }
                cache_size++;
                insert_before_(inlet, &n);
            }
        }
    };

    template<typename K, typename V>
    void check(
        LruCache<K, V>& cache,
        std::initializer_list<const V> nominated,
        std::initializer_list<const V> added,
        std::initializer_list<const V> reused) {
        auto n = cache.dummy.next;
        auto check_list = [&](std::initializer_list<const int> list, LruNodeBase* term, LruNodeBase::State state) {
            for (auto i = list.begin(); i != list.end(); i++) {
                if (n == term) {
                    ASSERT_EQ(i, list.end());
                    return;
                } else {
                    ASSERT_EQ(*i, (static_cast<LruNode<K, V>*>(n)->in_map->first));
                    ASSERT_EQ(int(state), int(n->state));
                    n = n->next;
                }
            }
            ASSERT_EQ(n, term);
        };
        check_list(nominated, cache.nomination, LruNodeBase::State::NOMINATED);
        check_list(added, cache.inlet, LruNodeBase::State::ADDED);
        check_list(reused, &cache.dummy, LruNodeBase::State::REUSED);
    }

    TEST(LruTest, Basic) {
        LruCache<int, int> cache(4, [](int v) { return v * 10; });
        check(cache, {}, {}, {});
        ASSERT_EQ(cache[0], 0);
        check(cache, { 0 }, {}, {});
        ASSERT_EQ(cache[1], 10);
        check(cache, { 0, 1 }, {}, {});
        ASSERT_EQ(cache[2], 20);
        check(cache, { 0, 1 }, { 2 }, {});
        ASSERT_EQ(cache[3], 30);
        check(cache, { 0, 1 }, { 3 }, { 2 });
        ASSERT_EQ(cache[4], 40);
        check(cache, { 1, 3 }, { 4 }, { 2 });
        ASSERT_EQ(cache[2], 20);
        check(cache, { 1, 3 }, { 4 }, { 2 });
        ASSERT_EQ(cache[4], 40);
        check(cache, { 1, 3 }, { 4 }, { 2 });
        ASSERT_EQ(cache[3], 30);
        check(cache, { 1, 4 }, { 2 }, { 3 });
        ASSERT_EQ(cache[5], 50);
        check(cache, { 4, 2 }, { 5 }, { 3 });
    }
}
