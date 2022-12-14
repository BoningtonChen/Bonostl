//
// Created by 陈奕锟 on 2022/12/3.
//

#ifndef BONOSTL_QUEUE_HPP
#define BONOSTL_QUEUE_HPP

#endif //BONOSTL_QUEUE_HPP
#pragma once

#include "bonostlpch.h"

namespace Bonostl
{

    template<typename T>
    class queue
    {
    private:
        struct node
        {
            std::shared_ptr<T> data;
            std::unique_ptr<node> next;

            explicit node(T i_data)
                    : data( std::move(i_data) ) {}
        };

        std::unique_ptr<node> head;
        node* tail;

    public:
        queue() : head(new node), tail( head.get() ) {}

        queue(const queue& other) = delete;

        queue &operator=(const queue& other) = delete;

        std::shared_ptr<T> try_pop()
        {
            if ( head.get() == tail )
                return std::shared_ptr<T>();

            std::shared_ptr<T> const res(head -> data);
            std::unique_ptr<node> const old_head = std::move(head);
            head = std::move(old_head -> next);

            return res;
        }

        void push(T new_value)
        {
            std::shared_ptr<T> new_data(
                    std::make_shared<T>( std::move(new_value) )
            );
            std::unique_ptr<node> p(new node);
            tail -> data = new_data;

            node* const new_tail = p.get();
            tail -> next = std::move(p);

            tail = new_tail;
        }

    };

}