/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2025 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef REENTRANTMAP_H
#define REENTRANTMAP_H

#include <memory>
#include <unordered_map>
#include <cassert>

class ReentrantMapIteratorException : private std::exception
{
public:
    virtual const char* what() const noexcept override
    {
        return "ReentrantMapIteratorException";
    }
};

/**
 * Map with iterator consisting of shared pointers. That way, the iterators don't get invalidated,
 * albeit possibly still orphaned. When orphaned, that will also break the chain for continuing iteration
 * later; it will think it's at end.
 *
 * Any new insertion gets added as head of the list, so iteration is in reverse of insertion order.
 */
template<typename K, typename V>
class ReentrantMap
{
    template<typename node_K, typename node_V>
    class node
    {
        friend class ReentrantMap<node_K, node_V>;

        std::weak_ptr<node<node_K, node_V>> next;
        std::weak_ptr<node<node_K, node_V>> prev;

        node(const node&) = delete;
        node(node&&) = delete;
        node &operator=(const node &)= delete;
        node &operator=(node &&)= delete;

    public:
        const K key;
        V val;

        node(const K &key, const V &val) :
            key(key),
            val(val)
        {

        }

        template<typename... Args>
        node(const K &key, Args... args) :
            key(key),
            val(args...)
        {

        }
    };

    ReentrantMap(const ReentrantMap&) = delete;
    ReentrantMap(ReentrantMap&&) = delete;
    ReentrantMap &operator=(const ReentrantMap &)= delete;
    ReentrantMap &operator=(ReentrantMap &&)= delete;

    std::shared_ptr<node<K,V>> head;
    std::unordered_map<K, std::shared_ptr<node<K,V>>> d;

public:

    class iterator
    {
        friend class ReentrantMap;

        std::weak_ptr<node<K,V>> cur;

    public:

        iterator() = default;
        iterator(const std::shared_ptr<node<K,V>> &val) :
            cur(val)
        {

        }

        iterator &operator++()
        {
            std::shared_ptr<node<K,V>> _cur = cur.lock();

            if (!_cur)
                return *this;

            cur = _cur->next;
            return *this;
        }

        bool operator!=(const ReentrantMap::iterator &other) const
        {
            const std::shared_ptr<node<K,V>> _cur = cur.lock();
            const std::shared_ptr<node<K,V>> _other_cur = other.cur.lock();

            if (_cur && _other_cur)
                return _cur->key != _other_cur->key;

            return static_cast<bool>(_cur) != static_cast<bool>(_other_cur);
        }

        bool operator==(const ReentrantMap::iterator &other) const
        {
            const std::shared_ptr<node<K,V>> _cur = cur.lock();
            const std::shared_ptr<node<K,V>> _other_cur = other.cur.lock();

            if (_cur && _other_cur)
                return _cur->key == _other_cur->key;

            return static_cast<bool>(_cur) == static_cast<bool>(_other_cur);
        }

        std::shared_ptr<node<K,V>> lock() const
        {
            return cur.lock();
        }

        void reset()
        {
            cur.reset();
        }
    };

    ReentrantMap() = default;

    V &operator[](const K &key)
    {
        auto x = try_emplace(key, {});
        return x.first.cur->val;
    }

    iterator begin() const
    {
        return iterator(head);
    }

    iterator end() const
    {
        return iterator();
    }

    template<typename... Args>
    std::pair<ReentrantMap::iterator, bool> try_emplace(const K &key, Args&&... args)
    {
        auto placement_result = d.try_emplace(key, std::make_shared<node<K,V>>(key, args...));

        if (placement_result.second)
        {
            std::shared_ptr<node<K,V>> old_head = head;
            head = placement_result.first->second;
            if (old_head)
                old_head->prev = head;
            head->next = old_head;
        }

        iterator return_iterator(placement_result.first->second);
        std::pair<ReentrantMap::iterator, bool> result(return_iterator, placement_result.second);
        return result;
    }

    void erase(ReentrantMap::iterator pos)
    {
        const std::shared_ptr<node<K,V>> val = pos.lock();

        if (!val)
            return;

        auto map_pos = d.find(val->key);

        if (map_pos == d.end())
            return;

        auto _prev = val->prev.lock();

        if (_prev)
            _prev->next = val->next;

        if (head == val)
            head = val->next.lock();

        auto _next = val->next.lock();

        if (_next)
            _next->prev = val->prev;

        d.erase(map_pos);
    }

    void erase(const K &key)
    {
        auto pos = find(key);
        erase(pos);
    }

    iterator find(const K &key)
    {
        auto pos = d.find(key);

        if (pos == d.end())
            return {};

        return iterator(pos->second);
    }

    bool empty() const
    {
        assert(d.empty() != static_cast<bool>(head));
        return d.empty();
    }

    size_t size() const
    {
        return d.size();
    }


};

#endif // REENTRANTMAP_H
