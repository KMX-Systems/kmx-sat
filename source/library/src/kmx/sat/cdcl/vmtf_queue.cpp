/// @file library/src/kmx/sat/cdcl/vmtf_queue.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/vmtf_queue.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/vmtf_queue.hpp>

namespace kmx::sat::cdcl
{
    void vmtf_queue::activate(const variable var) noexcept
    {
        const auto variable_index = static_cast<std::size_t>(var.index());
        if (node_index_of(variable_index) != npos)
            return;

        node node_value {var, npos, npos};
        const auto node_index = nodes_storage_.size();
        nodes_storage_.push_back(node_value);
        set_node_index(variable_index, node_index);
        if (tail_ == npos)
            head_ = node_index;
        else
        {
            nodes_storage_[tail_].next = node_index;
            nodes_storage_[node_index].previous = tail_;
        }
        tail_ = node_index;
        ++size_;
    }

    void vmtf_queue::bump(const variable var) noexcept
    {
        const auto node_index = node_index_of(static_cast<std::size_t>(var.index()));
        if ((node_index == npos) || (node_index == head_))
            return;

        unlink(node_index);
        {
            auto& node_value = nodes_storage_[node_index];
            node_value.previous = npos;
            node_value.next = head_;
        }
        nodes_storage_[head_].previous = node_index;
        head_ = node_index;
    }

    std::optional<variable> vmtf_queue::front_candidate() const noexcept
    {
        if (head_ == npos)
            return {};
        return nodes_storage_[head_].value;
    }

    void vmtf_queue::remove(const variable var) noexcept
    {
        const auto variable_index = static_cast<std::size_t>(var.index());
        const auto node_index = node_index_of(variable_index);
        if (node_index != npos)
        {
            unlink(node_index);
            erase_node_index(variable_index);
            --size_;
        }
    }

    void vmtf_queue::shuffle() noexcept
    {
        if (size_ <= 1u)
            return;

        const auto max_stride = static_cast<std::uint32_t>(size_ - 1u);
        const auto stride = static_cast<std::uint32_t>((shuffle_epoch_ % max_stride) + 1u);
        std::vector<std::size_t> order {};
        order.reserve(size_);
        for (auto current = head_; current != npos; current = nodes_storage_[current].next)
            order.push_back(current);
        std::rotate(order.begin(), order.begin() + stride, order.end());
        relink(order);
        last_shuffle_stride_ = stride;
        ++shuffle_epoch_;
    }

    void vmtf_queue::shuffle(const std::uint32_t salt) noexcept
    {
        if (size_ <= 1u)
            return;

        if (size_ > 2u)
        {
            const auto max_stride = static_cast<std::uint32_t>(size_ - 1u);
            shuffle_epoch_ = (shuffle_epoch_ + salt) % max_stride;
        }
        shuffle();
    }

    std::size_t vmtf_queue::node_index_of(const std::size_t variable_index) const noexcept
    {
        if (variable_index < direct_index_limit)
            return (variable_index < node_indices_.size()) ? node_indices_[variable_index] : npos;
        const auto it = overflow_node_indices_.find(variable_index);
        return (it == overflow_node_indices_.end()) ? npos : it->second;
    }

    void vmtf_queue::set_node_index(const std::size_t variable_index, const std::size_t node_index) noexcept
    {
        if (variable_index < direct_index_limit)
        {
            if (variable_index >= node_indices_.size())
                node_indices_.resize(variable_index + 1u, npos);
            node_indices_[variable_index] = node_index;
            return;
        }
        overflow_node_indices_[variable_index] = node_index;
    }

    void vmtf_queue::erase_node_index(const std::size_t variable_index) noexcept
    {
        if (variable_index < direct_index_limit)
        {
            if (variable_index < node_indices_.size())
                node_indices_[variable_index] = npos;
            return;
        }
        overflow_node_indices_.erase(variable_index);
    }

    void vmtf_queue::unlink(const std::size_t node_index) noexcept
    {
        const auto& node_value = nodes_storage_[node_index];
        const auto previous = node_value.previous;
        const auto next = node_value.next;
        if (previous == npos)
            head_ = next;
        else
            nodes_storage_[previous].next = next;
        if (next == npos)
            tail_ = previous;
        else
            nodes_storage_[next].previous = previous;
    }

    void vmtf_queue::relink(const std::vector<std::size_t>& order) noexcept
    {
        head_ = order.front();
        tail_ = order.back();
        for (std::size_t index {}; index < order.size(); ++index)
        {
            auto& current = nodes_storage_[order[index]];
            current.previous = (index == 0u) ? npos : order[index - 1u];
            current.next = (index + 1u == order.size()) ? npos : order[index + 1u];
        }
    }
}
