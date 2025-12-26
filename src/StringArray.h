#ifndef _STRINGARRAY_H_
#define _STRINGARRAY_H_

#include <string>
#include "esp_log.h"
#include <algorithm>
#include <functional>
#include <sys/stat.h>
#include "tools.h"
#include <strings.h>

/// @brief 定义一个模板类，用于存储值、下一个实例的指针
template <typename T>
class LinkedListNode {
public:
    LinkedListNode<T>* next;

    LinkedListNode(const T val)
        : next(nullptr)
        , m_value(val)
    {}
    ~LinkedListNode() {}
    const T &value() const
    {
        return m_value;
    }
    T &value()
    {
        return m_value;
    }
private:
    T   m_value;
};

/// @brief 定义一个接受储存指定类型值、储存方式模板的模板
/// @tparam T 储存的值
/// @tparam template<typename> class Item 储存方式为模板类，默认为LinkedListNode
template <typename T, template<typename> class Item = LinkedListNode>
class LinkedList {
public:
    typedef Item<T> ItemType;
private:
    class Iterator {
    public:
        Iterator(ItemType* current = nullptr) : m_node(current)
        {
            if (m_node != nullptr) {
                m_nextNode = current->next;
            }
        }
        Iterator(const Iterator &it) : m_node(it.m_node) {}
        Iterator &operator ++()
        {
            m_node = m_nextNode;
            m_nextNode = m_node != nullptr ? m_node->next : nullptr;
            return *this;
        }
        bool operator != (const Iterator &it) const
        {
            return m_node != it.m_node;
        }
        const T &operator * () const
        {
            return m_node->value();
        }
        const T* operator -> () const
        {
            return &m_node->value();
        }
    private:
        ItemType*   m_node;
        ItemType*   m_nextNode = nullptr;
    };
public:
    typedef const Iterator ConstIterator;
    typedef std::function<void(const T &)> OnRemove;
    typedef std::function<bool(const T &)> Predicate;   // 定义了统计条件函数
    LinkedList(OnRemove onRemove)
        : m_root(nullptr)
        , m_onRemove(onRemove)
    {}
    ~LinkedList()
    {
        free();
    }

    ConstIterator begin() const
    {
        return ConstIterator(m_root);
    }
    ConstIterator end() const
    {
        return ConstIterator(nullptr);
    }

    /// @brief 构造一个储存指定类型的节点并存储在队尾
    /// @param t
    void add(const T &t)
    {
        auto it = new ItemType(t);
        if (m_root == nullptr) {
            m_root = it;
        } else {
            auto i = m_root;
            while (i->next != nullptr) {
                i = i->next;
            }
            i->next = it;
        }
    }

    T &front() const
    {
        return m_root->value();
    }

    bool isEmpty() const
    {
        return m_root == nullptr;
    }

    // 可以优化该函数，但要增加存储
    size_t length() const
    {
        size_t len = 0;
        auto it = m_root;
        while (it != nullptr) {
            len++;
            it = it->next;
        }
        return len;
    }

    size_t count_if(Predicate predicate) const
    {
        size_t count = 0;
        auto it = m_root;
        while (it != nullptr) {
            if (predicate == nullptr) {
                count++;
            } else if (predicate(it->value())) {
                count++;
            }
            it = it->next;
        }
        return count;
    }

    const T* nth(size_t index) const
    {
        size_t i = 0;
        auto it = m_root;
        while (it != nullptr) {
            if (i++ == index) {
                return &(it->value());
            }
        }
        return nullptr;
    }

    bool remove(const T &t)
    {
        auto it = m_root;
        auto pit = m_root;
        while (it != nullptr) {
            if (it->value() == t) {
                if (it == m_root) {
                    m_root = m_root->next;
                } else {
                    pit->next = it->next;
                }
                if (m_onRemove != nullptr) {
                    m_onRemove(it->value());
                }
                delete it;
                return true;
            }
            pit = it;
            it = it->next;
        }
        return false;
    }

    bool remove_first(Predicate predicate)
    {
        auto it = m_root;
        auto pit = m_root;

        while (it != nullptr) {
            if (predicate(it->value())) {
                if (it == m_root) {
                    m_root = m_root->next;
                } else {
                    pit->next = it->next;
                }
                if (m_onRemove != nullptr) {
                    m_onRemove(it->value());
                }
                delete it;
                return true;
            }
            pit = it;
            it = it->next;
        }
        return false;
    }

    void free()
    {
        while (m_root != nullptr) {
            auto it = m_root;
            m_root = m_root->next;
            if (m_onRemove) {
                m_onRemove(it->value());
            }
            delete it;
        }
        m_root = nullptr;
    }

private:
    ItemType*   m_root;         // 根节点
    OnRemove    m_onRemove;     // 将元素移除的函数

};

class StringArray : public LinkedList<std::string> {
public:
    StringArray() : LinkedList(nullptr) {}
    bool containsIgnoreCase(const char* str)
    {
        for (const auto &s : *this) {
            if (strcasecmp(str, s.c_str()) == 0) {
                return true;
            }
        }
        return false;
    }
};

#endif