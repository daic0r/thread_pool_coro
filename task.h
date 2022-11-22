#ifndef TASK_H
#define TASK_H

#include <memory>
#include <type_traits>
#include <utility>

class task {
   class concept_interface;

   std::unique_ptr<concept_interface> m_pContained;

public:
   task() = default;

   template<typename Callable>
   task(Callable&& callable) : m_pContained{ std::make_unique<concept_impl<Callable>>(std::forward<Callable>(callable)) } {}

   task(const task& rhs) : m_pContained{ rhs.m_pContained->clone() } {}
   task& operator=(const task& rhs) {
      task copy{ rhs };
      swap(copy);
      return *this;
   }

   task(task&& rhs) noexcept = default;
   task& operator=(task&& rhs) noexcept = default;
   ~task() = default;


   void operator()() { (*m_pContained)(); }
   bool is_contained_object_copyable() const noexcept { return m_pContained->is_copyable(); }

private:
   class concept_interface {
   public:
      virtual ~concept_interface() = default;
      virtual std::unique_ptr<concept_interface> clone() const = 0;
      virtual void operator()() = 0;
      virtual bool is_copyable() const noexcept = 0;
   };

   template<typename Callable>
   class concept_impl : public concept_interface {
   public:
      template<typename U = Callable>
      concept_impl(U&& callable) : m_callable{ std::forward<U>(callable) } {}
      void operator()() override { m_callable(); }
      bool is_copyable() const noexcept override { return std::is_copy_constructible_v<Callable>; }
      std::unique_ptr<concept_interface> clone() const override { 
         if constexpr (std::is_copy_constructible_v<Callable>)
            return std::make_unique<concept_impl>(m_callable);
         else
            throw std::runtime_error("Callable is not copy-constructible!");
      }

   private:
      Callable m_callable;
   };

private:
   void swap(task& rhs) noexcept {
      using std::swap;
      swap(m_pContained, rhs.m_pContained);
   }

   friend void swap(task& lhs, task& rhs) noexcept {
      lhs.swap(rhs);
   }
};


#endif
