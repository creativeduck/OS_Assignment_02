// OS_Sim.cpp : 이 파일에는 'main' 함수가 포함됩니다. 거기서 프로그램 실행이 시작되고 종료됩니다.
//

#include <iostream>
#include <thread>
#include <Windows.h>
#include <mutex>              // std::mutex, std::unique_lock
#include <condition_variable>

#define PROCESS_READY   0
#define PROCESS_RUN     1
#define PROCESS_SLEEP   2

#define NUM_PROCESS 10

std::condition_variable cv;

struct proc_tbl_t {
    int id;
    int priority;
    volatile int state;
    int time_quantum;
    std::thread th;
    std::mutex mu_lock;
    struct proc_tbl_t* prev;
    struct proc_tbl_t* next;
} proc_tbl[10];

int nRun;
int run_proc0, run_proc1;
struct proc_tbl_t sleep_q;
struct proc_tbl_t ready_q;

void Put_Tail_Q(proc_tbl_t*, proc_tbl_t*);
proc_tbl_t* Get_Head_Q(proc_tbl_t* head);
void Print_Q(proc_tbl_t*);
void syscall_sleep(int id);
void syscall_wakeup();
void sys_scheduler();

void proc_timer_int(int id);  // Process 0: Timer Interrupt Generator
void proc_1(int id); // Process 1: Producer process
void proc_2(int id); // Process 2: Consumer Process
void proc_3(int id); // Idle process
void proc_4(int id); // Idle process

void proc_timer_int(int id)    /* Process 0: Timer Interrupt Genrator */
{
    proc_tbl_t* p;
    int sched_required;

    Sleep(100);
    for (;;) {
        sched_required = 0;

        std::cout << 't';
        if (run_proc0 != -1) {
            p = &(proc_tbl[run_proc0]);
            p->time_quantum--;
            if (p->time_quantum <= 0)
                sched_required = 1;
        }
        if (run_proc1 != -1) {
            p = &(proc_tbl[run_proc1]);
            p->time_quantum--;
            if (p->time_quantum <= 0)
                sched_required = 1;
        }
        if (sched_required) {
            sys_scheduler();
        }
        /* do Time Service */
        Sleep(100);
    }
}


void proc_1(int id)  /* Process 1: Producer Process */
{
    proc_tbl_t* p;
    std::mutex mtx_1;
    std::unique_lock<std::mutex> lk(mtx_1);

    p = &(proc_tbl[id]);
    for (;;) {
        while (p->state != PROCESS_RUN) {
            cv.wait(lk);
        }
        std::cout << id;
    }
}

void proc_2(int id)  /* Process 2: Consumer Process */
{
    proc_tbl_t* p;
    std::mutex mtx_2;
    std::unique_lock<std::mutex> lk(mtx_2);

    p = &(proc_tbl[id]);
    for (;;) {
        while (p->state != PROCESS_RUN) {
            cv.wait(lk);
        }
        std::cout << id;
    }
}

void proc_3(int id)  /* Process 3: Idle Process */
{
    proc_tbl_t* p;
    std::mutex mtx_3;
    std::unique_lock<std::mutex> lk(mtx_3);

    p = &(proc_tbl[id]);
    for (;;) {
        while (p->state != PROCESS_RUN) {
            cv.wait(lk);
        }
        std::cout << id;
    }
}

void proc_4(int id)  /* Process 4: Idle Process */
{
    proc_tbl_t* p;
    std::mutex mtx_4;
    std::unique_lock<std::mutex> lk(mtx_4);

    p = &(proc_tbl[id]);
    for (;;) {
        while (p->state != PROCESS_RUN) {
            cv.wait(lk);
        }
        std::cout << id;
    }
}

int main()  // Main thread: id = 0
{
    int main_state;
    proc_tbl_t* p;
    std::mutex mtx_main;
    std::unique_lock<std::mutex> lk(mtx_main);

    nRun = 0;
    run_proc0 = run_proc1 = -1;
    ready_q.next = ready_q.prev = &(ready_q);
    sleep_q.next = sleep_q.prev = &(sleep_q);

    p = &(proc_tbl[0]);
    p->id = 0;
    p->priority = 0;
    p->state = PROCESS_READY;
    p->th = std::thread(proc_timer_int, 0);
    //    p->th.hardware_concurrency();

    p = &(proc_tbl[1]);
    p->id = 1;
    p->priority = 4;
    p->state = PROCESS_READY;
    p->th = std::thread(proc_1, 1);
    Put_Tail_Q(&ready_q, p);
    //    p->th.hardware_concurrency();

    p = &(proc_tbl[2]);
    p->id = 2;
    p->priority = 3;
    p->state = PROCESS_READY;
    p->th = std::thread(proc_2, 2);
    Put_Tail_Q(&ready_q, p);
    //    p->th.hardware_concurrency();

    p = &(proc_tbl[3]);
    p->id = 3;
    p->priority = 0;
    p->state = PROCESS_READY;
    p->th = std::thread(proc_3, 3);
    Put_Tail_Q(&ready_q, p);
    //    p->th.hardware_concurrency();

    p = &(proc_tbl[4]);
    p->id = 4;
    p->priority = 1;
    p->state = PROCESS_READY;
    p->th = std::thread(proc_4, 4);
    Put_Tail_Q(&ready_q, p);
    //    p->th.hardware_concurrency();

    nRun = 0;
    run_proc0 = run_proc1 = -1;
    sys_scheduler();

    main_state = PROCESS_SLEEP;
    for (;;) { // Main() thread will sleep forever
        cv.wait(lk, [=] {return main_state == PROCESS_RUN; });
    }
    return 0;
}

void sys_scheduler()
{
    proc_tbl_t* p;

    if (run_proc0 == -1) {
        p = Get_Head_Q(&ready_q);
        if (p != NULL) {
            p->time_quantum = p->priority + 1;
            p->state = PROCESS_RUN;
            run_proc0 = p->id;
            nRun++;
            cv.notify_all();   // switch to process p->id and run */
        }
    }
    else {
        p = &(proc_tbl[run_proc0]);
        if (p->time_quantum <= 0) {
            p->state = PROCESS_READY;
            Put_Tail_Q(&ready_q, p);

            p = Get_Head_Q(&ready_q);
            if (p == NULL) {
                run_proc0 = -1;
                nRun--;
            }
            else {
                p->time_quantum = p->priority + 1;
                p->state = PROCESS_RUN;
                run_proc0 = p->id;
                cv.notify_all();   // switch to process p->id and run */
            }
        }
    }
    if (run_proc1 == -1) {
        p = Get_Head_Q(&ready_q);
        if (p != NULL) {
            p->time_quantum = p->priority + 1;
            p->state = PROCESS_RUN;
            run_proc1 = p->id;
            nRun++;
            cv.notify_all();   // switch to process p->id and run */
        }
    }
    else {
        p = &(proc_tbl[run_proc1]);
        if (p->time_quantum <= 0) {
            p->state = PROCESS_READY;
            Put_Tail_Q(&ready_q, p);

            p = Get_Head_Q(&ready_q);
            if (p == NULL) {
                run_proc1 = -1;
                nRun--;
            }
            else {
                p->time_quantum = p->priority + 1;
                p->state = PROCESS_RUN;
                run_proc1 = p->id;
                cv.notify_all();   // switch to process p->id and run */
            }
        }
    }
}


void syscall_sleep(int id)
{
    proc_tbl_t* p;

    p = &(proc_tbl[id]);
    p->state = PROCESS_SLEEP;
    if (p->id == run_proc0) {
        run_proc0 = -1;
        nRun--;
    }
    if (p->id == run_proc1) {
        run_proc1 = -1;
        nRun--;
    }
    Put_Tail_Q(&sleep_q, p);
    cv.notify_all();   // switch to process p->id and run */
}

void syscall_wakeup()
{
    proc_tbl_t* p;

    p = Get_Head_Q(&sleep_q);

    if (p == NULL) return;
    p->state = PROCESS_READY;
    Put_Tail_Q(&ready_q, p);
    cv.notify_all();   // switch to process p->id and run */
}


void Put_Tail_Q(proc_tbl_t* head, proc_tbl_t* item)
{
    (head->mu_lock).lock();
    item->prev = head->prev;
    head->prev->next = item;
    item->next = head;
    head->prev = item;
    (head->mu_lock).unlock();
}

proc_tbl_t* Get_Head_Q(proc_tbl_t* head)
{
    proc_tbl_t* item;

    (head->mu_lock).lock();
    if (head->next == head) {
        (head->mu_lock).unlock();
        return NULL;
    }
    item = head->next;

    item->next->prev = head;
    head->next = item->next;

    (head->mu_lock).unlock();
    return item;
}

void Print_Q(proc_tbl_t* head)
{
    proc_tbl_t* item;

    item = head->next;
    while (item != head) {
        std::cout << item->id << ' ';
        item = item->next;
    }
    std::cout << '\n';
}


// 프로그램 실행: <Ctrl+F5> 또는 [디버그] > [디버깅하지 않고 시작] 메뉴
// 프로그램 디버그: <F5> 키 또는 [디버그] > [디버깅 시작] 메뉴

// 시작을 위한 팁: 
//   1. [솔루션 탐색기] 창을 사용하여 파일을 추가/관리합니다.
//   2. [팀 탐색기] 창을 사용하여 소스 제어에 연결합니다.
//   3. [출력] 창을 사용하여 빌드 출력 및 기타 메시지를 확인합니다.
//   4. [오류 목록] 창을 사용하여 오류를 봅니다.
//   5. [프로젝트] > [새 항목 추가]로 이동하여 새 코드 파일을 만들거나, [프로젝트] > [기존 항목 추가]로 이동하여 기존 코드 파일을 프로젝트에 추가합니다.
//   6. 나중에 이 프로젝트를 다시 열려면 [파일] > [열기] > [프로젝트]로 이동하고 .sln 파일을 선택합니다.
