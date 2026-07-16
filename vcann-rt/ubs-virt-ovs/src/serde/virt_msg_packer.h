/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * ubs-virt-ovs is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef UBS_VIRT_OVS_MSG_PACKER_H
#define UBS_VIRT_OVS_MSG_PACKER_H

#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace virt::ovs {
class VirtMsgPacker {
public:
    /**
     * @breif Append a POD(Plain Old Data) struct
     *
     * @tparam T         [in] type of POD
     * @param val        [in] value of POD
     */
    template <typename T>
    void Serialize(const T &val, typename std::enable_if<std::is_trivially_copyable<T>::value, int>::type = 0)
    {
        outStream_.write(reinterpret_cast<const char *>(&val), sizeof(T));
    }

    /**
     * @breif Append a string
     *
     * @param val        [in] value of string
     */
    void Serialize(const std::string &val)
    {
        const uint32_t size = val.size();
        outStream_.write(reinterpret_cast<const char *>(&size), sizeof(size));
        outStream_.write(val.data(), size);
    }

    /**
     * @breif Append a pair
     *
     * @tparam K         [in] type of K of pair
     * @tparam V         [in] type of V of pair
     * @param val        [in] value of pair
     */
    template <typename K, typename V>
    void Serialize(const std::pair<K, V> &val)
    {
        Serialize(val.first);
        Serialize(val.second);
    }

    /**
     * @breif Append a vector
     *
     * @tparam V          [in] type of vector of element
     * @param container   [in] vector to be appended
     */
    template <typename V>
    void Serialize(const std::vector<V> &container)
    {
        const std::size_t size = container.size();
        outStream_.write(reinterpret_cast<const char *>(&size), sizeof(size));
        for (const auto &val : container) {
            Serialize(val);
        }
    }

    /**
     * @breif Append a map
     *
     * @tparam K          [in] type of map key
     * @tparam V          [in] type of map value
     * @param container   [in] map to be appended
     */
    template <typename K, typename V>
    void Serialize(const std::map<K, V> &container)
    {
        SerializeMap(container);
    }

    /**
     * @breif Append a unordered_map
     *
     * @tparam K          [in] type of unordered_map key
     * @tparam V          [in] type of unordered_map value
     * @param container   [in] unordered_map to be appended
     */
    template <typename K, typename V>
    void Serialize(const std::unordered_map<K, V> &container)
    {
        SerializeMap(container);
    }

    /**
     * @breif Serialize user-defined struct with member function
     *
     * @tparam T          [in] user-defined type
     * @param val         [in] value of user-defined type
     */
    template <typename T>
    auto Serialize(const T &val, typename std::enable_if<!std::is_trivially_copyable<T>::value, int>::type = 0)
        -> decltype(val.Serialize(std::declval<VirtMsgPacker &>()), void())
    {
        val.Serialize(*this);
    }

    /**
     * @breif Get the serialized result
     *
     * @return String value of serialized
     */
    std::string String() const
    {
        return outStream_.str();
    }

private:
    template <typename Map>
    void SerializeMap(const Map &container)
    {
        const std::size_t size = container.size();
        outStream_.write(reinterpret_cast<const char *>(&size), sizeof(size));
        for (const auto &[key, value] : container) {
            Serialize(key);
            Serialize(value);
        }
    }

    std::ostringstream outStream_;
};

class VirtMsgUnPacker {
public:
    /**
     * @breif Create unpacker with serialized data
     *
     * @param value         [in] serialized data with string type
     */
    explicit VirtMsgUnPacker(const std::string &value) : inStream_(value) {}

    /**
     * @breif Take data and deserialize to POD data
     *
     * @tparam T         [in] type of POD
     * @param val        [in/out] result data of POD
     */
    template <typename T>
    void Deserialize(T &val, typename std::enable_if<std::is_trivially_copyable<T>::value, int>::type = 0)
    {
        inStream_.read(reinterpret_cast<char *>(&val), sizeof(T));
    }

    /**
     * @breif Take data and deserialize to string
     *
     * @param val        [in/out] result data of string
     */
    void Deserialize(std::string &val)
    {
        uint32_t size = 0;
        inStream_.read(reinterpret_cast<char *>(&size), sizeof(size));
        val.resize(size);
        inStream_.read(&val[0], size);
    }

    /**
     * @breif Take data and deserialize to vector
     *
     * @tparam V          [in] type of vector of element
     * @param container   [in/out] result data of vector
     */
    template <typename V>
    void Deserialize(std::vector<V> &container)
    {
        std::size_t size = 0;
        inStream_.read(reinterpret_cast<char *>(&size), sizeof(size));
        container.clear();
        container.reserve(size);
        for (std::size_t i = 0; i < size; ++i) {
            V item;
            Deserialize(item);
            container.emplace_back(std::move(item));
        }
    }

    /**
     * @breif Take data and deserialize to map
     *
     * @tparam K          [in] type of map of element
     * @tparam V          [in] type of map of element
     * @param container   [in/out] result data of map
     */
    template <typename K, typename V>
    void Deserialize(std::map<K, V> &container)
    {
        DeserializeMap(container);
    }

    /**
     * @breif Take data and deserialize to unordered_map
     *
     * @tparam K          [in] type of unordered_map of element
     * @tparam V          [in] type of unordered_map of element
     * @param container   [in/out] result data of unordered_map
     */
    template <typename K, typename V>
    void Deserialize(std::unordered_map<K, V> &container)
    {
        DeserializeMap(container);
    }

    /**
     * @breif Deserialize for user-defined type (must have `Deserialize(VirtMsgUnPacker&)`)
     */
    template <typename T>
    auto Deserialize(T &val, typename std::enable_if<!std::is_trivially_copyable<T>::value, int>::type = 0)
        -> decltype(val.Deserialize(*this), void())
    {
        val.Deserialize(*this);
    }

private:
    template <typename Map>
    void DeserializeMap(Map &container)
    {
        std::size_t size = 0;
        inStream_.read(reinterpret_cast<char *>(&size), sizeof(size));
        container.clear();
        for (std::size_t i = 0; i < size; ++i) {
            typename Map::key_type key;
            typename Map::mapped_type value;
            Deserialize(key);
            Deserialize(value);
            container.emplace(std::move(key), std::move(value));
        }
    }

    std::istringstream inStream_;
};
} // namespace virt::ovs
#endif // UBS_VIRT_OVS_MSG_PACKER_H
