#include <iostream>
#include <thread>
#include <Windows.h>
#include <mutex>              // std::mutex, std::unique_lock
#include <condition_variable>
#include <atomic>			// for memory barrier

#define PROCESS_READY   0
#define PROCESS_RUN     1
#define PROCESS_SLEEP   2

#define TYPE_READY 0   // 레디 큐에 있는 프로세스를 출력하는 것이라고 알림
#define TYPE_SLEEP 1   // 블록 큐에 있는 프로세스를 출력하는 것이라고 알림

#define MAX_ITEM 20 // 생산자-소비자 아이템 최대 개수
#define FAVOUR_PRODUCER 0 // 생산자 차례
#define FAVOUR_CONSUMER 1 // 소비자 차례
#define ID_PRODUCER 1    // 생산자 ID
#define ID_CONSUMER 2    // 소비자 ID

#define FAVOUR_TIMER 3 // 타이머 인터럽트 차례
#define FAVOUR_SLEEP 4 // sleep 차례
#define ID_TIMER 3     // 타이머 인터럽트로 인한 스케쥴링인지 여부    
#define ID_SLEEP 4     // sleep 으로 인한 스케쥴링인지 여부


struct os_item {
    int num;		// 아이템 번호
    std::mutex mu_lock;
    struct os_item* prev;
    struct os_item* next;
};
// 생산자 - 소비자 변수
struct os_item os_item_q;			// 아이템 넣고 빼는 큐
std::atomic<int> item_count{ 0 };		// 아이템 개수
std::atomic<int> dekker_favoured{ FAVOUR_PRODUCER };	// dekker 알고리즘용 turn
std::atomic<bool> dekker_producer{ false };	// producer의 dekker 알고리즘용 변수
std::atomic<bool> dekker_consumer{ false };	// consumer의 dekker 알고리즘용 변수
// 생산자 아이템 인덱스 숫자 증가용 변수
std::atomic<int> next_item{ 0 };
// 스케쥴러와 타이머 인터럽트, sleep 함수 간 상호배제를 위한 변수
std::atomic<bool> sleep_flag{ false };
std::atomic<bool> scheduler_flag{ false };

std::condition_variable cv;
std::mutex cv_m;

struct proc_tbl_t {
    int id;
    int priority;
    int state;
    int time_quantum;
    std::thread th;
    std::mutex mu_lock;
    struct proc_tbl_t* prev;
    struct proc_tbl_t* next;
} proc_tbl[10];
volatile int nRun;
volatile int run_proc0, run_proc1;

struct proc_tbl_t sleep_q;
struct proc_tbl_t ready_q;

void Put_Tail_Q(proc_tbl_t*, proc_tbl_t*);
proc_tbl_t* Get_Head_Q(proc_tbl_t* head);
void Print_Q(proc_tbl_t* head, int type, int which);
void syscall_sleep(int id);
void syscall_wakeup();
void sys_scheduler(int which);

void proc_timer_int(int id);  // Process 0: Timer Interrupt Generator
void proc_1(int id); // Process 1: Producer process
void proc_2(int id); // Process 2: Consumer Process
void proc_3(int id); // Idle process
void proc_4(int id); // Idle process
// 아이템 관련 함수
void Put_Tail_Item(os_item* head, os_item* item);
os_item* Get_Head_Item(os_item* head);
os_item* Produce_Item();
void Consume_Item(os_item* item);

void proc_timer_int(int id)    /* Process 0: Timer Interrupt Genrator */
{
    proc_tbl_t* p;
    int sched_required;

    Sleep(100);
    for (;;) {
        // 스케쥴링이 실행중인 경우 scheduler_flag 는 true 입니다.
        // 프로세스가 sleep 되는 경우, sleep_flag 는 true입니다.
        // 따라서 스케쥴링이 실행중이지 않고, 또한 sleep 되는 프로세스가 없는 경우,
        // 타이머 인터럽트가 실행될 수 있도록 했습니다.
        // 이를 통해 스케쥴링이 이미 실행되고 있는데 타이머 인터럽트로 인해 또 스케쥴링이 되거나,
        // 또는 프로세스가 sleep 되고 있는데 타이머 인터럽트가 실행하여 중복으로 스케쥴링되는 걸 방지했습니다.
        if (!scheduler_flag.load() && !sleep_flag.load())
        {
            sched_required = 0;
            printf("\nTimer Interrupt\n");
            if (run_proc0 != -1) {
                p = &(proc_tbl[run_proc0]);
                p->time_quantum--;
                printf("%d process' time_quantum down\n", p->id);
                if (p->time_quantum <= 0)
                    sched_required = 1;
            }
            if (run_proc1 != -1) {
                p = &(proc_tbl[run_proc1]);
                p->time_quantum--;
                printf("%d process' time_quantum down\n", p->id);
                if (p->time_quantum <= 0)
                    sched_required = 1;
            }
            if (sched_required) {
                // 타이머 인터럽트가 실행 중인 상황에서, 
                // 타이머 인터럽트는 atomic 하게 실행되지 않기 때문에
                // 스케쥴링을 하기 전에 두 변수를 다시 체크해서 
                // 현재 스케쥴링이 실행중이지 않고, sleep 되는 프로세스가 없는 경우에만 스케쥴링을 진행합니다.
                if (!scheduler_flag.load() && !sleep_flag.load())
                {
                    printf("Scheduled from timer\n");
                    // 스케쥴링을 실행할 것이므로, 해당 변수를 true로 설정합니다.
                    scheduler_flag.store(true);
                    // TIMER 로부터의 스케쥴링임을 전달하면서, 스케쥴링합니다.
                    sys_scheduler(ID_TIMER);
                }
            }
        }
        Sleep(100);
    }
}
void proc_1(int id)  /* Process 1: Producer Process */
{
    os_item* item;  // 아이템 변수
    proc_tbl_t* p;
    std::unique_lock<std::mutex> lk(cv_m);

    p = &(proc_tbl[id]);
    for (;;) {
        // 스케쥴링이 실행중이지 않은 상황에서만 프로세스가 동작하도록 합니다.
        // 이를 통해, 이 프로세스가 실행되면서 sleep 되는 프로세스가 발생하는 걸 방지합니다.
        // 만일 스케쥴링이 실행되고 있는 상태에서, sleep 되는 프로세스가 발생하면,
        // 이로인해 중복 스케쥴링이 발생해 프로그램이 의도대로 동작하지 않았습니다.
        cv.wait(lk, [=] {return (!scheduler_flag.load() && p->state == PROCESS_RUN); });
        // 상호배제 부분
        // dekker 알고리즘을 사용해 구현했습니다.
        dekker_producer.store(true);
        while (dekker_consumer.load() == true)
        {
            if (dekker_favoured.load() == FAVOUR_CONSUMER)
            {
                dekker_producer.store(false);
                while (dekker_favoured.load() == FAVOUR_CONSUMER);
                dekker_producer.store(true);
            }
        }
        // critical section
        printf("\nProduce Item\n");
        item = Produce_Item();
        // 아이템 최대 개수를 초과하면, 재웁니다.
        if (item_count.load() >= MAX_ITEM)
        {
            // 이때 sleep_flag 를 true 로 설정해서, 프로세스가 sleep 되어야 한다고 알립니다.
            sleep_flag.store(true);
            printf("\nNo more item can be produced\n");
            // 상호배제 연산을 마무리합니다.
            dekker_favoured.store(FAVOUR_CONSUMER);
            dekker_producer.store(false);
            // 생산자 프로세스가 sleep 되어야 함을 전달하면서, syscall_sleep 함수를 호출합니다.
            syscall_sleep(ID_PRODUCER);
        }
        else
        {
            // 그렇지 않다면, 아이템 큐 맨 뒤에 아이템을 삽입합니다.
            Put_Tail_Item(&os_item_q, item);
            item_count.fetch_add(1);  // 아이템 개수를 1 증가시킵니다.
            printf("Item Inserted / Total Item : %d items \n", item_count.load());
            // 아이템 하나라도 있으면, 소비자 프로세스를 깨웁니다.
            if (item_count.load() == 1)
                syscall_wakeup();
            // 상호배제 부분
            dekker_favoured.store(FAVOUR_CONSUMER);
            dekker_producer.store(false);
            Sleep(10);
        }
    }
}
void proc_2(int id)  /* Process 2: Consumer Process */
{
    os_item* item;		// 아이템 변수 선언
    proc_tbl_t* p;
    std::unique_lock<std::mutex> lk(cv_m);

    p = &(proc_tbl[id]);
    for (;;) {
        // 스케쥴링이 실행중이지 않은 상황에서만 프로세스가 동작하도록 합니다.
        cv.wait(lk, [=] {return (!scheduler_flag.load() && p->state == PROCESS_RUN); });
        // dekker 알고리즘을 사용해 상호배제를 달성합니다.
        dekker_consumer.store(true);
        while (dekker_producer.load() == true)
        {
            if (dekker_favoured.load() == FAVOUR_PRODUCER)
            {
                dekker_consumer.store(false);
                while (dekker_favoured.load() == FAVOUR_PRODUCER);
                dekker_consumer.store(true);
            }
        }
        // critical section
        // 현재 아이템 개수가 0이면, 소비자 프로세스를 재웁니다.
        if (item_count.load() == 0)
        {
            sleep_flag.store(true); // sleep_flag 변수를 true로 설정합니다.
            printf("\nNo more item can be consumed\n");
            // 상호배제 부분
            dekker_favoured.store(FAVOUR_PRODUCER);
            dekker_consumer.store(false);
            syscall_sleep(ID_CONSUMER);
        }
        else
        {
            // 그렇지 않다면, 아이템을 하나 가져옵니다.
            item = Get_Head_Item(&os_item_q);
            item_count.fetch_sub(1); // 아이템 개수를 하나 감소시킵니다.
            printf("\nConsume Item / %d items left\n", item_count.load());
            // 아이탬 개수가 꽉 차지 않게 되면, 생산자를 깨웁니다.
            if (item_count.load() == MAX_ITEM - 1)
                syscall_wakeup();
            Consume_Item(item);
            // 상호배제 부분
            dekker_favoured.store(FAVOUR_PRODUCER);
            dekker_consumer.store(false);
            Sleep(10);
        }
    }
}
void proc_3(int id)  /* Process 3: Idle Process */
{
    proc_tbl_t* p;
    std::unique_lock<std::mutex> lk(cv_m);

    p = &(proc_tbl[id]);
    for (;;) {
        // 스케쥴링이 실행중이지 않은 상황에서만 프로세스가 동작하도록 합니다.
        cv.wait(lk, [=] {return (!scheduler_flag.load() && p->state == PROCESS_RUN); });
        printf("%d ", id);
        Sleep(10);
    }
}
void proc_4(int id)  /* Process 4: Idle Process */
{
    proc_tbl_t* p;
    std::unique_lock<std::mutex> lk(cv_m);

    p = &(proc_tbl[id]);
    for (;;) {
        // 스케쥴링이 실행중이지 않은 상황에서만 프로세스가 동작하도록 합니다.
        cv.wait(lk, [=] {return (!scheduler_flag.load() && p->state == PROCESS_RUN); });
        printf("%d ", id);
        Sleep(10);
    }
}
int main()  // Main thread: id = 0
{
    int main_state;
    proc_tbl_t* p;
    std::unique_lock<std::mutex> lk(cv_m);
    printf("운영체제 과제 2:  Producer & Consumer\n이름: 안광민\n학번: 2017280049\n제출일: 2022.05.10.\n");

    nRun = 0;
    run_proc0 = run_proc1 = -1;
    ready_q.next = ready_q.prev = &(ready_q);
    sleep_q.next = sleep_q.prev = &(sleep_q);
    // 아이템 큐 초기화
    os_item_q.next = os_item_q.prev = &(os_item_q);

    p = &(proc_tbl[0]);
    p->id = 0;
    p->priority = 0;
    p->state = PROCESS_READY;
    p->th = std::thread(proc_timer_int, 0);
    p->th.hardware_concurrency();

    p = &(proc_tbl[1]);
    p->id = 1;
    p->priority = 4;
    p->state = PROCESS_READY;
    p->th = std::thread(proc_1, 1);
    Put_Tail_Q(&ready_q, p);
    p->th.hardware_concurrency();

    p = &(proc_tbl[2]);
    p->id = 2;
    p->priority = 3;
    p->state = PROCESS_READY;
    p->th = std::thread(proc_2, 2);
    Put_Tail_Q(&ready_q, p);
    p->th.hardware_concurrency();

    p = &(proc_tbl[3]);
    p->id = 3;
    p->priority = 0;
    p->state = PROCESS_READY;
    p->th = std::thread(proc_3, 3);
    Put_Tail_Q(&ready_q, p);
    p->th.hardware_concurrency();

    p = &(proc_tbl[4]);
    p->id = 4;
    p->priority = 1;
    p->state = PROCESS_READY;
    p->th = std::thread(proc_4, 4);
    Put_Tail_Q(&ready_q, p);
    p->th.hardware_concurrency();

    nRun = 0;
    run_proc0 = run_proc1 = -1;
    sys_scheduler(ID_TIMER);

    // 그냥 sys_scheduler 함수 안 끝나도록 한 건가.
    main_state = PROCESS_SLEEP;
    for (;;) { // Main() thread will sleep forever
        cv.wait(lk, [=] {return main_state == PROCESS_RUN; });
    }
    return 0;
}

void sys_scheduler(int which)
{
    proc_tbl_t* p;
    printf("\nScheduling\n");
    if (run_proc0 == -1) {
        Print_Q(&ready_q, TYPE_READY, 0);   // 현재 ready_q 에 있는 프로세스들 출력
        p = Get_Head_Q(&ready_q);
        if (p != NULL) {
            p->time_quantum = p->priority + 1;
            p->state = PROCESS_RUN;
            run_proc0 = p->id;
            nRun++;
            printf("%d process is now run\n", p->id);
            // 프로세스가 sleep 하면서 스케쥴링된 것이었다면,
            // sleep_flag 을 false 로 설정해서 타이머 인터럽트가 다시 실행될 수 있도록 합니다.
            // 또한, 스케쥴링이 끝나면 scheduler_flag 를 false 로 설정해서,
            // 타이머 인터럽트와 프로세스들이 다시 실행될 수 있도록 합니다.
            // 아래에는 동일한 로직의 반복이므로, 설명은 여기서만 하겠습니다.
            if (which == ID_SLEEP)
            {
                sleep_flag.store(false);
                scheduler_flag.store(false);
                cv.notify_all();   // switch to process p->id and run */
            }
            else
            {
                scheduler_flag.store(false);
                cv.notify_all();   // switch to process p->id and run */
            }
        }
    }
    else {
        p = &(proc_tbl[run_proc0]);
        if (p->time_quantum <= 0) {
            p->state = PROCESS_READY;
            printf("%d process is turn to ready\n", p->id);
            Put_Tail_Q(&ready_q, p);
            Print_Q(&ready_q, TYPE_READY, 0);   // 현재 ready_q 에 있는 프로세스들 출력
            p = Get_Head_Q(&ready_q);
            if (p == NULL) {
                run_proc0 = -1;
                nRun--;
            }
            else {
                p->time_quantum = p->priority + 1;
                p->state = PROCESS_RUN;
                run_proc0 = p->id;
                printf("%d process is now run\n", p->id);
                if (which == ID_SLEEP)
                {
                    sleep_flag.store(false);
                    scheduler_flag.store(false);
                    cv.notify_all();   // switch to process p->id and run */
                }
                else
                {
                    scheduler_flag.store(false);
                    cv.notify_all();   // switch to process p->id and run */
                }
            }
        }
    }
    if (run_proc1 == -1) {
        Print_Q(&ready_q, TYPE_READY, 1);   // 현재 ready_q 에 있는 프로세스들 출력
        p = Get_Head_Q(&ready_q);
        if (p != NULL) {
            p->time_quantum = p->priority + 1;
            p->state = PROCESS_RUN;
            run_proc1 = p->id;
            nRun++;
            printf("%d process is now run\n", p->id);
            if (which == ID_SLEEP)
            {
                sleep_flag.store(false);
                scheduler_flag.store(false);
                cv.notify_all();   // switch to process p->id and run */
            }
            else
            {
                scheduler_flag.store(false);
                cv.notify_all();   // switch to process p->id and run */
            }
        }
    }
    else {
        p = &(proc_tbl[run_proc1]);
        if (p->time_quantum <= 0) {
            p->state = PROCESS_READY;
            Put_Tail_Q(&ready_q, p);
            printf("%d process is turn to ready\n", p->id);
            Print_Q(&ready_q, TYPE_READY, 1);   // 현재 ready_q 에 있는 프로세스들 출력
            p = Get_Head_Q(&ready_q);
            if (p == NULL) {
                run_proc1 = -1;
                nRun--;
            }
            else {
                p->time_quantum = p->priority + 1;
                p->state = PROCESS_RUN;
                run_proc1 = p->id;
                printf("%d process is now run\n", p->id);
                if (which == ID_SLEEP)
                {
                    sleep_flag.store(false);
                    scheduler_flag.store(false);
                    cv.notify_all();   // switch to process p->id and run */
                }
                else
                {
                    scheduler_flag.store(false);
                    cv.notify_all();   // switch to process p->id and run */
                }
            }
        }
    }
}
void syscall_sleep(int id)
{
    proc_tbl_t* p;
    // 스케쥴링이 실행중이지 않은 경우에만 프로세스를 sleep 합니다.
    // 이는 타이머 인터럽트가 실행중인 상황에서 sleep 이 요청된 경우,
    // sleep 을 지연하는 용도로 사용했습니다.
    // 타이머 인터럽트로 인한 스케쥴링이 발생하므로,
    // sleep 되어야 하는 프로세스는 여전히 아이템을 생산하거나 소비하지 못하며,
    // 타이머 인터럽트가 끝난 이후 여전히 해당 프로세스가 살아있다면,
    // 그때 sleep 하도록 해서 최대한 프로그램이 의도한 대로 동작하도록 했습니다.
    if (!scheduler_flag.load())
    {
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
        printf("%d process turn to sleep\n", p->id);
        scheduler_flag.store(true);
        sys_scheduler(ID_SLEEP);
    }
}

void syscall_wakeup()
{
    proc_tbl_t* p;
    Print_Q(&sleep_q, TYPE_SLEEP, -1);   // 현재 sleep_q 에 있는 프로세스들 출력
    p = Get_Head_Q(&sleep_q);

    if (p == NULL) return;
    p->state = PROCESS_READY;
    Put_Tail_Q(&ready_q, p);
    printf("%d process turn to wake\n", p->id);
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
void Print_Q(proc_tbl_t* head, int type, int which)
{
    if (type == TYPE_READY)
        if (which == 0)
            printf("\nsearch for first running process\ncurrent processes in ready_q : [ ");
        else
            printf("\nsearch for second running process\ncurrent processes in ready_q : [ ");
    else
        printf("\ncurrent processes in sleep_q : [ ");
    proc_tbl_t* item;
    item = head->next;
    while (item != head) {
        std::cout << item->id << ' ';
        item = item->next;
    }
    std::cout << "]\n";
}
// os_item 에서 아이템 빼기
// 기존 ready_q에서 프로세스를 가져오는 것과 동일한 로직입니다.
os_item* Get_Head_Item(os_item* head)
{
    os_item* item;

    if (head->next == head)
        return NULL;
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
// 아이템 삽입
// 기존 ready_q에 프로세스를 삽입하는 것과 동일한 로직입니다.
void Put_Tail_Item(os_item* head, os_item* item)
{
    (head->mu_lock).lock();
    item->prev = head->prev;
    head->prev->next = item;
    item->next = head;
    head->prev = item;
    (head->mu_lock).unlock();
}
// 아이템 생성
// 단순히 item 의 숫자를 1씩 증가하면서 아이템을 삽입했습니다.
os_item* Produce_Item()
{
    os_item* item = new os_item();
    item->num = next_item.load();
    next_item.fetch_add(1);
    return item;
}
// 아이템 소비
// 단순히 해당 아이템을 NULL로 했습니다..
void Consume_Item(os_item* item)
{
    item = NULL;
}