# 基于C++11的线程池

### 什么是线程池

**管理一个任务队列，一个线程队列，每次分配一个任务给一个线程，循环往复**

- **mutex**：锁，保证任务的添加和删除的互斥性
- **condition_variable**：条件变量，保证多个线程获取任务的同步性，当任务队列为空时，线程应该等待（阻塞）

### 线程安全的任务队列Task Queue

利用mutex来限制并发访问，避免当多个线程同时获取任务时出错

```c++
// 任务队列
template <class T>
class SafeQueue{
private:
    queue<T> m_queue; //利用模板函数构造队列，参数类型是函数
    mutex m_mutex; // 访问互斥信号量

public:
    SafeQueue() {}
    SafeQueue(SafeQueue &&other) {}
    ~SafeQueue() {}

    bool queue_empty(); // 判断队列是否为空
    int queue_size(); //返回队列的长度
    void enqueue(T &t); //向队列中添加任务函数
    bool dequeue(T &t); //从队列中取出任务
};
```

```c++
// 任务队列是否为空
template <class T>
bool SafeQueue<T>::queue_empty(){
    unique_lock<mutex> mylock(m_mutex); //互斥信号变量枷锁，防止m_queue被改变
    return m_queue.empty();
}

// 返回任务队列的大小
template <class T>
int SafeQueue<T>::queue_size(){
    unique_lock<mutex> mylock(m_mutex);
    return m_queue.size();
}

// 插入任务函数到队列中
template <class T>
void SafeQueue<T>::enqueue(T &t){
    unique_lock<mutex> mylock(m_mutex);
    m_queue.emplace(t);
}

// 从任务队列中取出任务
template <class T>
bool SafeQueue<T>::dequeue(T &t){
    unique_lock<mutex> mylock(m_mutex);
    if (m_queue.empty())
        return false;
    // 通过移动语义减少了不必要的拷贝构造
    t = move(m_queue.front()); //这里有疑问，t是左值引用
    m_queue.pop();
    return true;
}
```



### 线程池 Thread Pool

#### 提交函数——向任务队列中添加任务

接受任何参数的任何函数，包括普通函数、lambda表达式、成员函数

立即返回任务结束的结果，避免阻塞主线程

> c++11语义
>
> - `decltype(表达式)`：获取表达式的类型
>
> - `forward<Args>(args)... `：
>
>   前半部分利用完美转发，将参数包args中的每个元素以正确的类型进行转发，从而在函数调用时保持原始参数的引用类型；
>
>   ...表示将参数包中的每个元素展开并作为独立的参数传递给函数
>
> - `packaged_task`

```c++
// 提交函数：将任务插入到任务队列中
// 可变参数模板函数
// 异步任务调度器
// 尾返回类型推导
// 返回值是future对象，用于获取f函数的返回值
template <class F, class... Args>
auto ThreadPool::submit(F &&f, Args &&... args) -> future<decltype(f(args...))>{
    // 创建一个带有绑定参数的函数
    // 返回值为f的返回值，参数列表暂为空
    // 通过bind将函数f与参数包绑定
    function<decltype(f(args...))()> func = bind(forward<F>(f), forward<Args>(args)...);

    // 生成一个智能指针，指向的内容是func
    // 指向的类型是包装好的函数，返回值是f的返回值，参数列表为空
    auto task_ptr = make_shared<packaged_task<decltype(f(args...))()>>(task);

    // 创建一个函数对象，返回值为void，参数为空
    // 执行task_ptr
    function<void()> warpper_func = [task_ptr]()
    {
        (*task_ptr)(); //调用函数指针执行
    };

    // 队列通用安全封装好的函数，并压入安全队列中
    m_queue.enqueue(warpper_func);

    // 唤醒一个等待中的西安测绘给你
    m_conditional_lock.notify_one(); 

    // 返回先前的任务指针的future
    return task_ptr->get_future();
}
```

#### 内置工作线程

设立私有成员类来执行真正的工作

重载()操作进行了任务的取出与执行

```c++
// 内置工作线程类
    class ThreadWorker{
    private:
        int m_id; //工作id
        ThreadPool *m_pool; // 所属线程池
    public:
        // 构造函数
        ThreadWorker(ThreadPool *pool, const int id) : m_pool(pool), m_id(id) {}
        void operator()(); //重载()操作
    };
```

```c++
// 重载()操作，嵌套类在外围类的外部定义，需要限定嵌套类的名字
// 使得对象可以像函数一样被调用
void ThreadPool::ThreadWorker::operator()(){
    // 定义基础函数类func，返回值为void,参数为空
    function<void()> func;
    bool dequeued; // 是否正在取出队列中的元素
    // 判断线程池是否关闭，没有关闭则从任务队列中循环提取任务
    while (!m_pool->m_shutdown){
        {
            // 构造大括号，unique_lock的作用范围
            unique_lock<mutex> m_lock(m_pool->m_conditional_mutex);
            // 如果任务队列为空，阻塞，等待提交函数插入新的任务
            if (m_pool->m_queue.queue_empty()){
                m_pool->m_conditional_lock.wait(m_lock); // 等待唤醒
            }
            // 取出任务队列中的元素,并移动给func
            dequeued = m_pool->m_queue.dequeue(func);
        }
        if (dequeued)
            func(); //如果成功取出，执行工作函数
    }
}
```

使用了一个while循环，在线程池处于工作时循环从任务队列中提取任务。并利用条件变量，在任务队列为空时阻塞当前线程，等待上文中的提交函数添加任务后发出的通知。在任务队列不为空时，我们将任务队列中的任务取出，并放在事先声明的基础函数类*func*中。成功取出后便立即执行该任务

### 线程池代码

解释下面的语句为什么会调用threadworker类中的重载()，在当前线程不断循环的执行任务函数

```
m_threads.at(i) = thread(ThreadWorker(this, i));
```

等价于

```
ThreadWorker worker(this, i);
thread t(worker);
```

第一句创建一个对象worker，传递的指针是当前线程池的this指针和线程id

第二句将worker对象传递给线程，该线程将使用worker对象的重载()作为线程的执行函数（会调用`worker()`）

总结：创建一个新线程，这个线程会执行 `ThreadWorker` 类的 `operator()` 函数，从而在工作线程中循环地提取并执行任务，直到线程池被关闭。

### 项目完整代码

#### thread_pool.h

```c++
// thread_pool.h
#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <bits/stdc++.h>
using namespace std;

// 任务队列
template <class T>
class SafeQueue{
private:
    queue<T> m_queue; //利用模板函数构造队列，参数类型是函数
    mutex m_mutex; // 访问互斥信号量

public:
    SafeQueue() {}
    SafeQueue(SafeQueue &&other) {}
    ~SafeQueue() {}

    bool queue_empty(); // 判断队列是否为空
    int queue_size(); //返回队列的长度
    void enqueue(T &t); //向队列中添加任务函数
    bool dequeue(T &t); //从队列中取出任务
};


// 线程池
class ThreadPool{
private:
    // 内置工作线程类
    class ThreadWorker{
    private:
        int m_id; //工作id
        ThreadPool *m_pool; // 所属线程池
    public:
        // 构造函数
        ThreadWorker(ThreadPool *pool, const int id) : m_pool(pool), m_id(id) {}
        void operator()(); //重载()操作
    };

    bool m_shutdown; // 线程池是否关闭
    SafeQueue<function<void()>> m_queue; //因为已经封装好了任务函数为void()
    vector<thread> m_threads; //工作线程队列
    mutex m_conditional_mutex; // 线程休眠互斥变量
    condition_variable m_conditional_lock; // 条件变量，让线程处于休眠或者唤醒

public:
    // 构造函数，默认线程数为4，默认初始化vector为4个thread，默认线程池关闭
    ThreadPool(const int n_threads = 4) : m_threads(vector<thread>(n_threads)), m_shutdown(false) {}
    ThreadPool(const ThreadPool &) = delete; // 将复制构造函数设为删除，不能通过复制进行拷贝构造
    ThreadPool(ThreadPool &&) = delete; // 将移动构造函数设置为删除
    ThreadPool &operator=(const ThreadPool &) = delete; // 将复制赋值运算符设为删除
    ThreadPool &operator=(ThreadPool &&) = delete; //将移动赋值运算符设为删除

    void init(); //创建线程池
    void shutdown(); // 关闭线程池
    // 提交函数
    template <typename F, typename... Args>
    auto submit(F &&f, Args &&...args) -> std::future<decltype(f(args...))>;
};

#endif
```

#### thread_pool.cpp

```c++
#include "thread_pool.h"

// 任务队列是否为空
template <class T>
bool SafeQueue<T>::queue_empty(){
    unique_lock<mutex> mylock(m_mutex); //互斥信号变量枷锁，防止m_queue被改变
    return m_queue.empty();
}

// 返回任务队列的大小
template <class T>
int SafeQueue<T>::queue_size(){
    unique_lock<mutex> mylock(m_mutex);
    return m_queue.size();
}

// 插入任务函数到队列中
template <class T>
void SafeQueue<T>::enqueue(T &t){
    unique_lock<mutex> mylock(m_mutex);
    m_queue.emplace(t);
}

// 从任务队列中取出任务
template <class T>
bool SafeQueue<T>::dequeue(T &t){
    unique_lock<mutex> mylock(m_mutex);
    if (m_queue.empty())
        return false;
    // 通过移动语义减少了不必要的拷贝构造
    t = move(m_queue.front()); //这里有疑问，t是左值引用
    m_queue.pop();
    return true;
}


// 提交函数：将任务插入到任务队列中
// 可变参数模板函数
// 异步任务调度器
// 尾返回类型推导
// 返回值是future对象，用于获取f函数的返回值
template <class F, class... Args>
auto ThreadPool::submit(F &&f, Args &&... args) -> future<decltype(f(args...))>{
    // 创建一个带有绑定参数的函数
    // 返回值为f的返回值，参数列表暂为空
    // 通过bind将函数f与参数包绑定
    function<decltype(f(args...))()> func = bind(forward<F>(f), forward<Args>(args)...);

    // 生成一个智能指针，指向的内容是func
    // 指向的类型是包装好的函数，返回值是f的返回值，参数列表为空
    auto task_ptr = make_shared<packaged_task<decltype(f(args...))()>>(func);

    // 创建一个函数对象，返回值为void，参数为空
    // 执行task_ptr
    function<void()> warpper_func = [task_ptr]()
    {
        (*task_ptr)(); //调用函数指针执行
    };

    // 队列通用安全封装好的函数，并压入安全队列中
    m_queue.enqueue(warpper_func);

    // 唤醒一个等待中的线程
    m_conditional_lock.notify_one(); 

    // 返回先前的任务指针的future
    return task_ptr->get_future();
}

// 重载()操作，嵌套类在外围类的外部定义，需要限定嵌套类的名字
// 使得对象可以像函数一样被调用
void ThreadPool::ThreadWorker::operator()(){
    // 定义基础函数类func，返回值为void,参数为空
    function<void()> func;
    bool dequeued; // 是否正在取出队列中的元素
    // 判断线程池是否关闭，没有关闭则从任务队列中循环提取任务
    while (!m_pool->m_shutdown){
        {
            // 构造大括号，unique_lock的作用范围
            unique_lock<mutex> m_lock(m_pool->m_conditional_mutex);
            // 如果任务队列为空，阻塞，等待提交函数插入新的任务
            if (m_pool->m_queue.queue_empty()){
                m_pool->m_conditional_lock.wait(m_lock); // 等待唤醒
            }
            // 取出任务队列中的元素,并移动给func
            dequeued = m_pool->m_queue.dequeue(func);
        }
        if (dequeued)
            func(); //如果成功取出，执行工作函数
    }
}

// 创建一个线程池
void ThreadPool::init(){
    for (int i = 0; i < m_threads.size(); i++){
        // 分配工作线程
        // ThreadWorker(this, i)：创建一个工作线程对象，传递当前pool指针this和id
        // thread()：创建一个新的线程，入口时worker对象
        // 将创建的线程对象分配给线程池中m_threads向量中的相应位置
        m_threads.at(i) = thread(ThreadWorker(this, i));   
    }
}

// 当所有线程执行完毕任务，关闭线程池
void ThreadPool::shutdown(){
    m_shutdown = true;
    m_conditional_lock.notify_all(); //唤醒所有工作线程
    for (int i = 0; i < m_threads.size(); i++){
        if (m_threads.at(i).joinable()){
            // 如果线程在等待
            m_threads.at(i).join(); //将线程加入等待队列
        }
    }
}
```

#### 测试样例

```c++
#include <iostream>
#include <random>
#include "thread_pool.cpp"
using namespace std;

std::random_device rd; // 真实随机数产生器

std::mt19937 mt(rd()); //生成计算随机数mt

std::uniform_int_distribution<int> dist(-1000, 1000); //生成-1000到1000之间的离散均匀分布数

auto rnd = std::bind(dist, mt);

// 设置线程睡眠时间
void simulate_hard_computation()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(2000 + rnd()));
}

// 添加两个数字的简单函数并打印结果
void multiply(const int a, const int b)
{
    simulate_hard_computation();
    const int res = a * b;
    std::cout << a << " * " << b << " = " << res << std::endl;
}

// 添加并输出结果
void multiply_output(int &out, const int a, const int b)
{
    simulate_hard_computation();
    out = a * b;
    std::cout << a << " * " << b << " = " << out << std::endl;
}

// 结果返回
int multiply_return(const int a, const int b)
{
    simulate_hard_computation();
    const int res = a * b;
    std::cout << a << " * " << b << " = " << res << std::endl;
    return res;
}

void example()
{
    // 创建3个线程的线程池
    ThreadPool pool(3);

    // 初始化线程池
    pool.init();

    // 提交乘法操作，总共30个
    for (int i = 1; i <= 3; ++i)
        for (int j = 1; j <= 10; ++j)
        {
            pool.submit(multiply, i, j);
        }

    // 使用ref传递的输出参数提交函数
    int output_ref;
    auto future1 = pool.submit(multiply_output, std::ref(output_ref), 5, 6);

    // 等待乘法输出完成
    future1.get();
    std::cout << "Last operation result is equals to " << output_ref << std::endl;

    // 使用return参数提交函数
    auto future2 = pool.submit(multiply_return, 5, 3);

    // 等待乘法输出完成
    int res = future2.get();
    std::cout << "Last operation result is equals to " << res << std::endl;

    // 关闭线程池
    pool.shutdown();
}

int main()
{
    example();

    return 0;
}
```

### 

