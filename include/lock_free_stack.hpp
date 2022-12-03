//
// Created by 陈奕锟 on 2022/12/3.
//

#ifndef BONOSTL_LOCK_FREE_STACK_HPP
#define BONOSTL_LOCK_FREE_STACK_HPP

#endif //BONOSTL_LOCK_FREE_STACK_HPP

#include "bonostlpch.h"

template<typename T>
class lock_free_stack
{
private:
    struct node
    {
        T data;
        node* next;
        node(T const& i_data)
            : data(i_data)
        {}
    };

    std::atomic<node*> head;

public:
    void push(T const& data)
    {
        node* const new_node = new node(data);
        new_node->next = head.load();

        while ( !head.compare_exchange_weak(new_node->next, new_node) );
    }
};