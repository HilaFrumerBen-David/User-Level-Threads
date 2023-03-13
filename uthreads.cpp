//
// Created by omer_siton on 30/03/2022.
//
#define MILLION 1000000
#include <stdio.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdbool.h>
#include <iostream>
#include <set>
#include <deque>
#include <vector>
#include <unordered_map>
#include <csetjmp>

#define BLOCK sigprocmask(SIG_BLOCK, &set, NULL)
#define UNBLOCK sigprocmask(SIG_UNBLOCK, &set, NULL)
#define MAX_THREAD_NUM 100 /* maximal number of threads */
#define STACK_SIZE 4096 /* stack size per thread (in bytes) */

#define READY 1
#define RUNNING 2
#define BLOCKED 3
#define SLEEP 4
#define SLEEPBLOCK 5

#define JB_SP 6
#define JB_PC 7


#define DEF_NO_TID_ERR "thread library error: no thread with tid exists"
#define DEF_VIRTUAL_TIME_ERR "system error: virtual timer error"
#define DEF_NON_POS_ERR "thread library error: quantum_usecs is non-positive"
#define DEF_NULL_ENTREY_POINT_ERR "thread library error: the function entry_point is nullptr"
#define DEF_THREAD_LIMIT_ERR "thread library error: the number of concurrent threads to exceed the limit"
#define DEF_BLOCK_MAIN_ERR "thread library error: can't block main thread"
#define DEF_SLEEP_MAIN_THREAD_ERR "thread library error: can't sleep main thread"

typedef unsigned long address_t;
typedef void (*thread_entry_point)(void);

/**
 * this class characterize the thread
 */
class Thread{
        int tid_; // Thread ID
        int state_; // State: ready/blocked/running/sleep
        int  t_runs_counter_; // number of times the cur thread was running
        char stack_[STACK_SIZE]; // thread stack
        int num_quantums_sleep_; // > 0 if this thread is sleeping

     public:

        sigjmp_buf env_; // thread env
        /// Ctor
        explicit Thread(const int tid, const int state = READY):
        tid_(tid), state_(state), t_runs_counter_(0), stack_(), num_quantums_sleep_(0), env_(){}
        /// Dtor
        ~Thread() = default;
        ///Get
        int getTid() { return tid_; }
        int getState() const { return state_; }
        int getTCounter() const { return t_runs_counter_; }
        __jmp_buf_tag *getEnv(){ return env_; }
        const char *getStack() { return stack_; }
        int getNumQuantumsSleep() const { return num_quantums_sleep_;}
        ///Set
        void setState(int state) { state_ = state; }
        void setTCounter() { t_runs_counter_++; }
        void setNumQuantumsSleep(int numQuantumsSleep) { num_quantums_sleep_ = numQuantumsSleep; }
        ///Func
        void subNumQuantumsSleep() { num_quantums_sleep_ --; }
};

/// Scheduler
int total_num_of_quantums_;
int running_thread_;
std::vector<int> sleep_{};
std::deque<int> ready_{};
std::set<int> tids_{}; // tid min
std::unordered_map<int, Thread*> all_threads_{};

/**
 * initialize the library containers
 */
void init_lib ()
{
    for(int i = 0; i < MAX_THREAD_NUM; ++i){
        tids_.insert(i);
    }
    total_num_of_quantums_ = 0;
    running_thread_ = 0;
}
/**
 *
 * @return the next available minimal tid
 */
int extract_min(){
    int res = *tids_.begin();
    tids_.erase(tids_.begin());
    return res;
}
/**
 * erase tid from ready queue
 * @param tid thread id
 */
void erase_thread_from_ready(int tid){
    for(auto it = ready_.begin(); it != ready_.end(); )
    {
        if(*it == tid){
            it = ready_.erase(it);
        }
        else
            ++it;
    }
}
/**
 * erase tid from sleep vector
 * @param tid thread id
 */
void erase_thread_from_sleep(int tid){
    for(auto it = sleep_.begin(); it != sleep_.end(); )
    {
        if(*it == tid){
            it = sleep_.erase(it);
        }
        else
            ++it;
    }
}

struct sigaction sa = {0};
struct itimerval timer;
sigset_t  set;
/**
 * reduce by one the num of quantum's for each thread in sleep vector
 */
void handle_sleep_threads()
{
    for (auto it = sleep_.begin(); it != sleep_.end();){
        all_threads_[*it]->subNumQuantumsSleep();
        if (all_threads_[*it]->getNumQuantumsSleep() == 0)
        {

            if (all_threads_[*it]->getState() == SLEEPBLOCK){
                all_threads_[*it]->setState(BLOCKED);
            }
            else{
                all_threads_[*it]->setState(READY);
                ready_.push_back(*it);
            }
            it = sleep_.erase(it);
        }
        else
            ++it;
    }
}

/**
 * initialize the virtual timer
 */
void virtual_timer_initialize(){
    // Start a virtual timer. It counts down whenever this process is executing.
    if (setitimer(ITIMER_VIRTUAL, &timer, NULL))
    {
        std::cerr << DEF_VIRTUAL_TIME_ERR << std::endl;
        exit(1);
    }
}


void jump_to_thread(int tid)
{
    UNBLOCK;
    siglongjmp(all_threads_[tid]->env_, 1);
}


/**
 * @brief Saves the current thread state, and jumps to the other thread.
 */
void timer_handler(int sig){
    /// happens just between switches
    BLOCK;
    int cur_tid = running_thread_;
    all_threads_[cur_tid]->setTCounter();
    total_num_of_quantums_++;
    if(all_threads_[cur_tid]->getState() == RUNNING )
    {
        all_threads_[cur_tid]->setState(READY);
        ready_.push_back(cur_tid);
    }
    /// handle in sleep threads and case of zero
    handle_sleep_threads();
    /// save cur thread and run next thread
    int ret_val = sigsetjmp(all_threads_[cur_tid]->env_, 1);
    bool did_just_save_bookmark = ret_val == 0;
    //    bool did_jump_from_another_thread = ret_val != 0;
    if (did_just_save_bookmark)
    {
        int next_tid =ready_.front();
        ready_.pop_front();
        all_threads_[next_tid]->setState(RUNNING);
        running_thread_ = next_tid;
        jump_to_thread(next_tid);
    }
}

/**
 * switch between threads when the first terminated
 */
void terminate_handler(){
    /// happens just between switches
    int cur_tid = running_thread_;
    total_num_of_quantums_++;
    /// handle in sleep threads and case of zero
    handle_sleep_threads();
    /// save cur thread and run next thread
    int next_tid =ready_.front();
    ready_.pop_front();
    all_threads_[next_tid]->setState(RUNNING);
    running_thread_ = next_tid;
    jump_to_thread(next_tid);
}


/** A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}


/**
 * @brief initializes the thread library.
 *
 * You may assume that this function is called before any other thread library function, and that it is called
 * exactly once.
 * The input to the function is the length of a quantum in micro-seconds.
 * It is an error to call this function with non-positive quantum_usecs.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_init(int quantum_usecs){
    if (quantum_usecs <= 0)
    {
        std::cerr << DEF_NON_POS_ERR << std::endl;
        return -1;
    }
    /// create class scheduler
    init_lib();
    /// create main thread with allocation
    int tid = extract_min();
    all_threads_[tid] = new Thread(tid, RUNNING);
    running_thread_ = tid;
    /// sigprocmask
    sigemptyset(&set);
    sigaddset(&set, SIGVTALRM);
    /// Install timer_handler as the signal handler for SIGVTALRM.
    sa.sa_handler = &timer_handler;
    if (sigaction(SIGVTALRM, &sa, NULL) < 0)
    {
        std::cerr << "system error: sigaction error" << std::endl;
        exit(1);
    }
    /// Configure the timer to expire after quantom usec...
    timer.it_value.tv_sec = quantum_usecs / MILLION;        // first time interval, seconds part
    timer.it_value.tv_usec = quantum_usecs % MILLION;        // first time interval, microseconds part
    /// configure the timer to expire every 0 sec after that.
    timer.it_interval.tv_sec = quantum_usecs / MILLION;    // following time intervals, seconds part
    timer.it_interval.tv_usec = quantum_usecs % MILLION;    // following time intervals, microseconds part
    virtual_timer_initialize();
    return 0;
}

/**
 * @brief Creates a new thread, whose entry point is the function entry_point with the signature
 * void entry_point(void).
 *
 * The thread is added to the end of the READY threads list.
 * The uthread_spawn function should fail if it would cause the number of concurrent threads to exceed the
 * limit (MAX_THREAD_NUM).
 * Each thread should be allocated with a stack of size STACK_SIZE bytes.
 *
 * @return On success, return the ID of the created thread. On failure, return -1.
*/
int uthread_spawn(thread_entry_point entry_point)
{
    BLOCK;
    /// initializes env[tid] to use the right stack, and to run from the function 'entry_point', when we'll use
    /// siglongjmp to jump into the thrchar *ead.
    if (entry_point == nullptr)
    {
        std::cerr << DEF_NULL_ENTREY_POINT_ERR << std::endl;
        UNBLOCK;
        return -1;
    }
    if (all_threads_.size() >= MAX_THREAD_NUM)
    {
        std::cerr << DEF_THREAD_LIMIT_ERR << std::endl;
        UNBLOCK;
        return -1;
    }
    /// size thread < MAX_THREAD_NUM
    int next_tid = extract_min();
    all_threads_[next_tid] = new Thread(next_tid, READY);
    Thread* new_thread = all_threads_[next_tid];
    ready_.push_back(new_thread->getTid());
    address_t sp = (address_t) new_thread->getStack() + STACK_SIZE - sizeof(address_t);
    address_t pc = (address_t) entry_point;
    sigsetjmp(new_thread->getEnv(), 0);
    (new_thread->getEnv()->__jmpbuf)[JB_SP] = translate_address(sp);
    (new_thread->getEnv()->__jmpbuf)[JB_PC] = translate_address(pc);
    sigemptyset(&new_thread->getEnv()->__saved_mask);
    UNBLOCK;
    return new_thread->getTid();
}

/**
 * @brief Terminates the thread with ID tid and deletes it from all relevant control structures.
 *
 * All the resources allocated by the library for this thread should be released. If no thread with ID tid exists it
 * is considered an error. Terminating the main thread (tid == 0) will result in the termination of the entire
 * process using exit(0) (after releasing the assigned library memory).
 *
 * @return The function returns 0 if the thread was successfully terminated and -1 otherwise. If a thread terminates
 * itself or the main thread is terminated, the function does not return.
*/
int uthread_terminate(int tid){
    BLOCK;
    if (tid == 0){
        for(auto & kv : all_threads_){
            delete kv.second;
            kv.second = nullptr;
        }
        all_threads_.clear();
        UNBLOCK;
        exit(0);
    }
    if (all_threads_.count(tid) == 0){
        std::cerr << DEF_NO_TID_ERR << std::endl;
        UNBLOCK;
        return -1;
    }

    int state = all_threads_[tid]->getState();
    /// delete thread from map and insert tid to heap min
    delete all_threads_[tid];
    all_threads_.erase(tid);
    tids_.insert(tid);

    if (state == READY){
        erase_thread_from_ready(tid);
    }
    if (state == RUNNING){
        virtual_timer_initialize();
        terminate_handler();

    }
    if (state == SLEEP || state == SLEEPBLOCK){
        erase_thread_from_sleep(tid);
    }
    UNBLOCK;
    return 0;
}

/**
 * @brief Blocks the thread with ID tid. The quantumsthread may be resumed later using uthread_resume.
 *
 * If no thread with ID tid exists it is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision should be made. Blocking a thread in
 * BLOCKED state has no effect and is not considered an error.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_block(int tid)
{
    BLOCK;
    if (tid == 0)
    {
        std::cerr << DEF_BLOCK_MAIN_ERR << std::endl;
        UNBLOCK;
        return -1;
    }
    if (all_threads_.count(tid) == 0){
        std::cerr << DEF_NO_TID_ERR << std::endl;
        UNBLOCK;
        return -1;
    }
    int state = all_threads_[tid]->getState();
    if (state != BLOCKED)
    {
        if (state == READY){
            erase_thread_from_ready(tid);
            all_threads_[tid]->setState(BLOCKED);
        }
        if (state == RUNNING){
            all_threads_[tid]->setState(BLOCKED);
            virtual_timer_initialize();
            timer_handler(0); //todo check signal num
        }
        if (state == SLEEP){
            all_threads_[tid]->setState(SLEEPBLOCK);

        }
    }
    UNBLOCK;
    return 0;
}

/**
 * @brief Resumes a blocked thread with ID tid and moves it to the READY state.
 *
 * Resuming a thread in a RUNNING or READY state has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_resume(int tid){
    BLOCK;
    if (all_threads_.count(tid) ==0)
    {
        std::cerr << DEF_NO_TID_ERR << std::endl;
        UNBLOCK;
        return -1;
    }
    if (all_threads_[tid]->getState() == BLOCKED)
    {
        all_threads_[tid]->setState(READY);
        ready_.push_back(tid);
    }
    if (all_threads_[tid]->getState() == SLEEPBLOCK)
    {
        all_threads_[tid]->setState(SLEEP);
    }
    UNBLOCK;
    return 0;
}


/**
 * @brief Blocks the RUNNING thread for num_quantums quantums.
 *
 * Immediately after the RUNNING thread transitions to the BLOCKED state a scheduling decision should be made.
 * After the sleeping time is over, the thread should go back to the end of the READY threads list.
 * The number of quantums refers to the number of times a new quantum starts, regardless of the reason. Specifically,
 * the quantum of the thread which has made the call to a thread blocks itself, a scheduling decision should be made.o uthread_sleep isn’t counted.
 * It is considered an error if the main thread (tid==0) calls this function.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_sleep(int num_quantums)
{
    BLOCK;
    if (num_quantums <= 0)
    {
        std::cerr << DEF_NON_POS_ERR << std::endl;
        UNBLOCK;
        return -1;
    }
    if (running_thread_ == 0)
    {
        std::cerr << DEF_SLEEP_MAIN_THREAD_ERR << std::endl;
        UNBLOCK;
        return -1;
    }
    int cur_thread = running_thread_;
    sleep_.push_back(cur_thread);
    all_threads_[cur_thread]->setNumQuantumsSleep(num_quantums);
    all_threads_[cur_thread]->setState(SLEEP);
    /// to switch with the next thread
    virtual_timer_initialize();
    timer_handler(2); //todo check signal
    UNBLOCK;
    return 0;
}

/**
 * @brief Returns the thread ID of the calling thread.
 *
 * @return The ID of the calling thread.
*/
int uthread_get_tid()
{
    BLOCK;
    UNBLOCK;
    return running_thread_;
}

/**
 * @brief Returns the total number of quantums since the library was initialized, including the current quantum.
 *
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number should be increased by 1.
 *
 * @return The total number of quantums.
*/
int uthread_get_total_quantums(){
    BLOCK;
    UNBLOCK;
    return total_num_of_quantums_ + 1;
}

/**
 * @brief Returns the number of quantums the thread with ID tid was in RUNNING state.
 *
 * On the first time a thread runs, the function should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state when this function is called, include
 * also the current quantum). If no thread with ID tid exists it is considered an error.
 *
 * @return On success, return the number of quantums of the thread with ID tid. On failure, return -1.
*/
int uthread_get_quantums(int tid){
    BLOCK;
    if (!all_threads_.count(tid)){
        std::cerr << DEF_NO_TID_ERR << std::endl;
        UNBLOCK;
        return -1;
    }
    if (running_thread_ == tid){
        UNBLOCK;
        return all_threads_[tid]->getTCounter() + 1;
    }
    UNBLOCK;
    return all_threads_[tid]->getTCounter();
}


