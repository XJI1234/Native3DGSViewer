#pragma once
#include "gs_server/windows_client.h"
#include <iterator>
#include <list>
#include <optional>
#include <set>

namespace gs::server
{
class PacketCache
{
  public:
    explicit PacketCache(size_t budget = 64ull << 20) : budget_(budget)
    {
    }
    std::optional<Download> get(const std::string &key)
    {
        for (auto entry = entries_.begin(); entry != entries_.end(); ++entry)
            if (entry->first == key)
            {
                entries_.splice(entries_.begin(), entries_, entry);
                return entries_.front().second;
            }
        return std::nullopt;
    }
    void put(const std::string &key, Download value)
    {
        if (value.packet.size() > budget_)
            return;
        for (auto entry = entries_.begin(); entry != entries_.end(); ++entry)
            if (entry->first == key)
            {
                bytes_ -= entry->second.packet.size();
                entries_.erase(entry);
                break;
            }
        bytes_ += value.packet.size();
        entries_.emplace_front(key, std::move(value));
        while (bytes_ > budget_ || entries_.size() > 256)
        {
            auto victim = entries_.end();
            for (auto entry = entries_.end(); entry != entries_.begin();)
            {
                --entry;
                if (!protected_.contains(entry->first))
                {
                    victim = entry;
                    break;
                }
            }
            if (victim == entries_.end())
                victim = std::prev(entries_.end());
            bytes_ -= victim->second.packet.size();
            entries_.erase(victim);
        }
    }
    void clear()
    {
        entries_.clear();
        protected_.clear();
        bytes_ = 0;
    }
    size_t bytes() const
    {
        return bytes_;
    }
    bool contains(const std::string &key) const
    {
        for (const auto &entry : entries_)
            if (entry.first == key)
                return true;
        return false;
    }
    void erase(const std::string &key)
    {
        for (auto entry = entries_.begin(); entry != entries_.end(); ++entry)
            if (entry->first == key)
            {
                bytes_ -= entry->second.packet.size();
                entries_.erase(entry);
                return;
            }
    }
    void protect(const std::vector<std::string> &keys)
    {
        protected_ = std::set<std::string>(keys.begin(), keys.end());
    }

  private:
    size_t budget_, bytes_ = 0;
    std::list<std::pair<std::string, Download>> entries_;
    std::set<std::string> protected_;
};
} // namespace gs::server
