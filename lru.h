#include <iostream>
#include <list>
#include <string>
#include <unordered_map>

namespace toy {
template <class Key, class Value>
class LRUCache {
    struct Node {
        Key k;
        Value v;
    };

    int size;
    int capacity;
    std::list<Node> nodes;
    using list_it = std::list<Node>::iterator;
    std::unordered_map<Key, list_it> table;

   public:
    LRUCache(int max_capacity) : capacity(max_capacity), size(0) {}

    void put(Key key, Value value) {
        if (table.contains(key)) {
            Node node{key, value};
            nodes.erase(table[key]);  // remove existing entry
            nodes.push_front(node);
            table[key] = nodes.begin();
            return;
        }
        if (size >= capacity) {
            Key key = nodes.back().k;  // last key
            std::cout << "remove: " << key << std::endl;
            nodes.pop_back();
            table.erase(key);  // remove last element from table
            size--;
        }
        Node node{key, value};
        nodes.push_front(node);
        table[key] = nodes.begin();
        size++;
    }

    Value get(Key key) {
        if (table.contains(key)) {
            Node node{key, table[key]->v};  // create new node
            nodes.erase(table[key]);        // remove existing entry
            nodes.push_front(node);
            table[key] = nodes.begin();
            return table[key]->v;
        }
        throw std::runtime_error("key not found");
    }
};
}  // namespace toy

/*
LFU

// dict: key -> freq
// dict: freq -> head of freq list

least ---------> most
f1 -> f2 -> f3 -> f4
|     |
|     |
|     |
*/