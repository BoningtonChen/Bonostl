#pragma once

#include "bonostlpch.h"

namespace Bonostl
{
    /// Move-only type-erased callable wrapper, used to store tasks in thread_pool.
    class function_wrapper
    {
    public:
        function_wrapper() = default;

        template<typename F>
        function_wrapper(F&& func)
            : impl_(std::make_unique<impl_type<F>>(std::forward<F>(func)))
        {
        }

        function_wrapper(function_wrapper&&) noexcept = default;
        function_wrapper& operator=(function_wrapper&&) noexcept = default;

        function_wrapper(const function_wrapper&) = delete;
        function_wrapper& operator=(const function_wrapper&) = delete;

        void operator()()
        {
            if (!impl_)
            {
                throw std::bad_function_call();
            }

            impl_->call();
        }

    private:
        struct impl_base
        {
            virtual void call() = 0;
            virtual ~impl_base() = default;
        };

        template<typename F>
        struct impl_type : impl_base
        {
            explicit impl_type(F&& func)
                : func_(std::move(func))
            {
            }

            void call() override
            {
                func_();
            }

            F func_;
        };

        std::unique_ptr<impl_base> impl_;
    };
}
