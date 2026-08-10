#pragma once

#include "bonostlpch.h"

namespace Bonostl
{
    template<typename Key, typename Value, typename Hash = std::hash<Key>>
    class threadsafe_lookup_table
    {
    private:
        class bucket_type
        {
        private:
            using bucket_value = std::pair<Key, Value>;
            using bucket_data = std::list<bucket_value>;

        public:
            std::optional<Value> value_for(Key const& key) const
            {
                std::shared_lock lock(mutex_);

                auto const found_entry = find_entry_for(key);
                return found_entry == data_.end()
                    ? std::nullopt
                    : std::optional<Value>(found_entry->second);
            }

            void add_or_update_mapping(Key const& key, Value const& value)
            {
                std::unique_lock lock(mutex_);
                auto const found_entry = find_entry_for(key);

                if (found_entry == data_.end())
                {
                    data_.emplace_back(key, value);
                }
                else
                {
                    found_entry->second = value;
                }
            }

            void remove_mapping(Key const& key)
            {
                std::unique_lock lock(mutex_);
                auto const found_entry = find_entry_for(key);

                if (found_entry != data_.end())
                {
                    data_.erase(found_entry);
                }
            }

            std::map<Key, Value> snapshot() const
            {
                std::shared_lock lock(mutex_);
                return {data_.begin(), data_.end()};
            }

        private:
            using bucket_iterator = typename bucket_data::iterator;
            using bucket_const_iterator = typename bucket_data::const_iterator;

            bucket_iterator find_entry_for(Key const& key)
            {
                return std::ranges::find_if(data_, [&](bucket_value const& item) {
                    return item.first == key;
                });
            }

            bucket_const_iterator find_entry_for(Key const& key) const
            {
                return std::ranges::find_if(data_, [&](bucket_value const& item) {
                    return item.first == key;
                });
            }

            bucket_data data_;
            mutable std::shared_mutex mutex_;
        };

    public:
        using key_type = Key;
        using mapped_type = Value;
        using hash_type = Hash;

        explicit threadsafe_lookup_table(
            unsigned num_buckets = 19, Hash const& hasher = Hash())
            : buckets_(num_buckets), hasher_(hasher)
        {
            for (auto& bucket : buckets_)
            {
                bucket = std::make_unique<bucket_type>();
            }
        }

        threadsafe_lookup_table(const threadsafe_lookup_table&) = delete;
        threadsafe_lookup_table& operator=(const threadsafe_lookup_table&) = delete;

        std::optional<Value> value_for(Key const& key) const
        {
            return get_bucket(key).value_for(key);
        }

        void add_or_update_mapping(Key const& key, Value const& value)
        {
            get_bucket(key).add_or_update_mapping(key, value);
        }

        void remove_mapping(Key const& key)
        {
            get_bucket(key).remove_mapping(key);
        }

        std::map<Key, Value> get_map() const
        {
            std::map<Key, Value> result;

            for (auto const& bucket : buckets_)
            {
                result.merge(bucket->snapshot());
            }

            return result;
        }

    private:
        bucket_type& get_bucket(Key const& key) const
        {
            std::size_t const bucket_index = hasher_(key) % buckets_.size();
            return *buckets_[bucket_index];
        }

        std::vector<std::unique_ptr<bucket_type>> buckets_;
        Hash hasher_;
    };
}
