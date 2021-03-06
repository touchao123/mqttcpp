#ifndef BROKER_H_
#define BROKER_H_

#include "dtypes.hpp"
#include "message.hpp"
#include <vector>
#include <deque>
#include <string>
#include <unordered_map>
#include <memory>
#include <iostream>
#include <boost/algorithm/string.hpp>
#include "gsl.h"


template<typename S> class Subscription;


template<typename S>
class MqttBroker {
public:

    MqttBroker(bool useCache = false):
        _useCache{useCache}
    {
    }

    void subscribe(S& subscriber, std::vector<std::string> topics) {
        std::vector<MqttSubscribe::Topic> newTopics(topics.size());
        std::transform(topics.cbegin(), topics.cend(), newTopics.begin(),
                       [](auto t) { return MqttSubscribe::Topic(t, 0); });
        subscribe(subscriber, newTopics);
    }

    void subscribe(S& subscriber, std::vector<MqttSubscribe::Topic> topics) {
        invalidateCache();
        for(const auto& topic: topics) {
            std::vector<std::string> subParts;
            boost::split(subParts, topic.topic, boost::is_any_of("/"));
            auto& node = addOrFindNode(_tree, subParts);
            node.leaves.emplace_back(Subscription<S>{&subscriber, topic.topic});
        }
    }

    void unsubscribe(const S& subscriber) {
        std::vector<std::string> topics{};
        unsubscribe(subscriber, topics);
    }

    void unsubscribe(const S& subscriber, const std::vector<std::string>& topics) {
        invalidateCache();
        unsubscribeImpl(_tree, subscriber, topics);
    }

    void publish(const char* topic, gsl::span<const ubyte> bytes) {
        publish(gsl::ensure_z(topic), bytes);
    }

    void publish(const gsl::cstring_span<> topicSpan, gsl::span<const ubyte> bytes) {
        const auto topic = gsl::to_string(topicSpan);

        if(_useCache && _cache.find(topic) != _cache.end()) {
            for(auto subscriber: _cache[topic]) subscriber->newMessage(bytes);
            return;
        }

        std::vector<std::string> pubParts;
        boost::split(pubParts, topic, boost::is_any_of("/"));
        publishImpl(_tree, pubParts, topic, bytes);
    }

private:

    struct Node;
    using NodePtr = std::shared_ptr<Node>;

    struct Node {
        std::unordered_map<std::string, NodePtr> children;
        std::vector<Subscription<S>> leaves;
    };


    bool _useCache;
    std::unordered_map<std::string, std::vector<S*>> _cache;
    Node _tree;

    void invalidateCache() {
        if(_useCache) _cache.clear();
    }

    static Node& addOrFindNode(Node& tree, gsl::span<std::string> parts) {
        if(parts.size() == 0) return tree;

        const auto part = parts[0]; //copying is good here
        //create if not already here
        if(tree.children.find(part) == tree.children.end()) {
            tree.children[part] = std::make_shared<Node>();
        }

        parts = parts.sub(1);
        return addOrFindNode(*tree.children[part], parts);
    }

    void publishImpl(Node& tree, gsl::span<std::string> pubParts,
                     const std::string& topic, gsl::span<const ubyte> bytes) {

        if(pubParts.size() == 0) return;

        const std::string front = pubParts[0];
        pubParts = pubParts.sub(1);

        for(const auto& part: std::vector<std::string>{front, "#", "+"}) {
            if(tree.children.find(part) != tree.children.end()) {
                Node& node = *tree.children[part];
                if(pubParts.size() == 0 || part == "#") publishNode(node, topic, bytes);

                if(pubParts.size() == 0 && node.children.find("#") != node.children.end()) {
                    //So that "finance/#" matches "finance"
                    publishNode(*node.children["#"], topic, bytes);
                }

                publishImpl(node, pubParts, topic, bytes);
            }
        }
    }


    void publishNode(Node& node, const std::string& topic, gsl::span<const ubyte> bytes) {
        for(auto& subscription: node.leaves) {
            subscription.subscriber->newMessage(bytes);
            if(_useCache) _cache[topic].emplace_back(subscription.subscriber);
        }
    }

    void unsubscribeImpl(Node& tree, const S& subscriber, const std::vector<std::string>& topics) {
        (void)tree; (void)subscriber; (void)topics;

        decltype(tree.leaves) leaves;
        std::copy_if(tree.leaves.cbegin(), tree.leaves.cend(), std::back_inserter(leaves),
                     [&subscriber, &topics](auto&& a) {
                         return !a.isSubscriber(subscriber, topics);
                     });
        std::swap(tree.leaves, leaves);

        if(tree.children.size() == 0) return;

        for(const auto& item: tree.children) {
            unsubscribeImpl(*item.second, subscriber, topics);
        }
    }
};

template<typename S>
class Subscription {
public:

    Subscription(gsl::not_null<S*> subscriber, gsl::cstring_span<> topic) noexcept :
        subscriber{subscriber}, topic{gsl::to_string(topic)} {
    }

    bool isSubscriber(const S& sub, const std::vector<std::string>& topics) const {
        const auto isSameTopic = topics.size() == 0 ||
            std::find(topics.cbegin(), topics.cend(), topic) != topics.end();
        return isSameTopic && subscriber == &sub;
    }

    gsl::not_null<S*> subscriber;
    std::string topic;
};


#endif // BROKER_H_
