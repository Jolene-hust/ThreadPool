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
