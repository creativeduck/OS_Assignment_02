#include <iostream>
#include <thread>
#include <Windows.h>
#include <mutex>              // std::mutex, std::unique_lock
#include <condition_variable>
#include <atomic>			// for memory barrier

#define PROCESS_READY   0
#define PROCESS_RUN     1
#define PROCESS_SLEEP   2

#define TYPE_READY 0   // ���� ť�� �ִ� ���μ����� ����ϴ� ���̶�� �˸�
#define TYPE_SLEEP 1   // ��� ť�� �ִ� ���μ����� ����ϴ� ���̶�� �˸�

#define MAX_ITEM 20 // ������-�Һ��� ������ �ִ� ����
#define FAVOUR_PRODUCER 0 // ������ ����
#define FAVOUR_CONSUMER 1 // �Һ��� ����
#define ID_PRODUCER 1
#define ID_CONSUMER 2

#define FAVOUR_TIMER 3 // Ÿ�̸� ���ͷ�Ʈ ����
#define FAVOUR_SLEEP 4 // sleep ����
#define ID_TIMER 3
#define ID_SLEEP 4


struct os_item {
    int num;		// ������ ��ȣ
    std::mutex mu_lock;
    struct os_item* prev;
    struct os_item* next;
};
// ������ - �Һ��� ����
struct os_item os_item_q;			// ������ �ִ� ť
std::atomic<int> item_count{ 0 };		// ������ ����
std::atomic<int> dekker_favoured{ FAVOUR_PRODUCER };	// dekker �˰���� turn
std::atomic<bool> dekker_producer{ false };	// producer�� dekker �˰���� ����
std::atomic<bool> dekker_consumer{ false };	// consumer�� dekker �˰���� ����
// ������ ������ ���� ��
std::atomic<int> next_item{ 0 };
// �����췯�� Ÿ�̸� ���ͷ�Ʈ, sleep �Լ� �� ��ȣ������ ���� ����
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
// ������ ���� �Լ�
void Put_Tail_Item(os_item* head, os_item* item);
os_item* Get_Head_Item(os_item* head);
os_item* Produce_Item();
void Consume_Item(int item);

void proc_timer_int(int id)    /* Process 0: Timer Interrupt Genrator */
{
    proc_tbl_t* p;
    int sched_required;

    Sleep(100);
    for (;;) {
        if (!scheduler_flag.load() && !sleep_flag.load())
        {
            // ���� sleep ���������� ���� ��쿡��, ���ͷ�Ʈ ����
// �����층�� �ߺ��Ǿ ������� �ʵ��� �ϱ� ����
//timer_flag.store(true);
//cv.notify_all();

            sched_required = 0;
            printf("\nTimer Interrupt\n");
            // ���μ��� 0�� ���������� ������,
            if (run_proc0 != -1) {
                // 0 ���μ��� �����ͼ� �����층 �ʿ��ϴٰ� �����Ѵ�.
                p = &(proc_tbl[run_proc0]);
                p->time_quantum--;
                printf("%d process' time_quantum down\n", p->id);
                if (p->time_quantum <= 0)
                    sched_required = 1;
            }
            // ���μ��� 1�� ���������� ������,
            if (run_proc1 != -1) {
                // 1 ���μ��� �����ͼ� �����층 �ʿ��ϴٰ� �����Ѵ�.
                p = &(proc_tbl[run_proc1]);
                p->time_quantum--;
                printf("%d process' time_quantum down\n", p->id);
                if (p->time_quantum <= 0)
                    sched_required = 1;
            }
            // �� �� �ϳ��� �����층 �ʿ��ϸ�, �����층�Ѵ�.
            if (sched_required) {
                if (!scheduler_flag.load() && !sleep_flag.load())
                {
                    printf("Scheduled from timer\n");
                    scheduler_flag.store(true);
                    sys_scheduler(ID_TIMER);
                }
            }
        }
        Sleep(100);
    }
}
void proc_1(int id)  /* Process 1: Producer Process */
{
    os_item* item;  // ������ ����
    proc_tbl_t* p;
    std::unique_lock<std::mutex> lk(cv_m);

    p = &(proc_tbl[id]);
    for (;;) {
        // sleep �����층�� ���������� �ʰ�, Ÿ�̸� ���ͷ�Ʈ�� ���������� ���� ��쿡�� �����Ѵ�.
        cv.wait(lk, [=] {return (!scheduler_flag.load() && p->state == PROCESS_RUN); });
        // ��ȣ���� �κ�
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
        // ������ �ִ� ������ �ʰ��ϸ�, ����
        if (item_count.load() >= MAX_ITEM)
        {
            sleep_flag.store(true);
            //cv.notify_all();
            printf("\nNo more item can be produced\n");
            dekker_favoured.store(FAVOUR_CONSUMER);
            dekker_producer.store(false);
            syscall_sleep(ID_PRODUCER);

            //if (scheduler_flag)
            //{
            //    // ���� �����층 ���̶��, �Ѿ��
            //    dekker_favoured.store(FAVOUR_CONSUMER);
            //    dekker_producer.store(false);
            //    Sleep(10);
            //}
            //else
            //{
            //    sleep_flag.store(true);
            //    //cv.notify_all();
            //    printf("\nNo more item can be produced\n");
            //    dekker_favoured.store(FAVOUR_CONSUMER);
            //    dekker_producer.store(false);
            //    syscall_sleep(ID_PRODUCER);
            //}
        }
        else
        {
            // �׷��� �ʴٸ�, ������ ť �� �ڿ� ������ ����
            Put_Tail_Item(&os_item_q, item);
            item_count.fetch_add(1);
            printf("Item Inserted / Total Item : %d items \n", item_count.load());
            // ������ �ϳ��� ������, �Һ��� ���μ��� �����
            if (item_count.load() == 1)
                syscall_wakeup();
            // ��ȣ���� �κ�
            dekker_favoured.store(FAVOUR_CONSUMER);
            dekker_producer.store(false);
            Sleep(10);
        }
    }
}
void proc_2(int id)  /* Process 2: Consumer Process */
{
    os_item* item;		// ������ ���� ����
    proc_tbl_t* p;
    std::unique_lock<std::mutex> lk(cv_m);

    p = &(proc_tbl[id]);
    for (;;) {
        // sleep �����층�� ���������� �ʰ�, Ÿ�̸� ���ͷ�Ʈ�� ���������� ���� ��쿡�� �����Ѵ�.
        cv.wait(lk, [=] {return (!scheduler_flag.load() && p->state == PROCESS_RUN); });
        // ��ȣ���� �κ�
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
        // ���� ������ ������ 0�̸�, ����
        if (item_count.load() == 0)
        {
            sleep_flag.store(true);
            printf("\nNo more item can be consumed\n");
            // ��ȣ���� �κ�
            dekker_favoured.store(FAVOUR_PRODUCER);
            dekker_consumer.store(false);
            syscall_sleep(ID_CONSUMER);
            //if (scheduler_flag.load())
            //{
            //    // ���� �����층 ���̶��, �׳� �Ѿ��
            //    printf("\nPass\n");
            //    dekker_favoured.store(FAVOUR_PRODUCER);
            //    dekker_consumer.store(false);
            //    Sleep(10);
            //}
            //else
            //{
            //    sleep_flag.store(true);
            //    printf("\nNo more item can be consumed\n");
            //    // ��ȣ���� �κ�
            //    dekker_favoured.store(FAVOUR_PRODUCER);
            //    dekker_consumer.store(false);
            //    syscall_sleep(ID_CONSUMER);
            //}
        }
        else
        {
            item = Get_Head_Item(&os_item_q);
            item_count.fetch_sub(1);
            printf("\nConsume Item / %d items left\n", item_count.load());
            // ������ ������ �� ���� �ʰ� �Ǹ�, ������ �����
            if (item_count.load() == MAX_ITEM - 1)
                syscall_wakeup();
            // ������ �Һ��ϴ� ����(�̰� ���� �Ҳ�.. ���� �ʿ��Ѱ�?)
            Consume_Item(item->num);
            // ��ȣ���� �κ�
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
        // sleep �����층�� ���������� �ʰ�, Ÿ�̸� ���ͷ�Ʈ�� ���������� ���� ��쿡�� �����Ѵ�.
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
        // sleep �����층�� ���������� �ʰ�, Ÿ�̸� ���ͷ�Ʈ�� ���������� ���� ��쿡�� �����Ѵ�.
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
    printf("�ü�� ���� 2:  Producer & Consumer\n�̸�: �ȱ���\n�й�: 2017280049\n������: 2022.05.10.\n");

    nRun = 0;
    run_proc0 = run_proc1 = -1;
    ready_q.next = ready_q.prev = &(ready_q);
    sleep_q.next = sleep_q.prev = &(sleep_q);
    // ������ ť �ʱ�ȭ
    os_item_q.next = os_item_q.prev = &(os_item_q);

    p = &(proc_tbl[0]);
    p->id = 0;
    p->priority = 0;
    p->state = PROCESS_READY;
    p->th = std::thread(proc_timer_int, 0);
    p->th.hardware_concurrency();

    p = &(proc_tbl[1]);
    p->id = 1;
    p->priority = 24;
    p->state = PROCESS_READY;
    p->th = std::thread(proc_1, 1);
    Put_Tail_Q(&ready_q, p);
    p->th.hardware_concurrency();

    p = &(proc_tbl[2]);
    p->id = 2;
    p->priority = 23;
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

    // �׳� sys_scheduler �Լ� �� �������� �� �ǰ�.
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
        Print_Q(&ready_q, TYPE_READY, 0);   // ���� ready_q �� �ִ� ���μ����� ���
        p = Get_Head_Q(&ready_q);
        if (p != NULL) {
            p->time_quantum = p->priority + 1;
            p->state = PROCESS_RUN;
            run_proc0 = p->id;
            nRun++;
            printf("%d process is now run\n", p->id);
            // ���μ����� sleep �ϸ鼭 �����층�� ���̾��ٸ�,
            // sleep �� false �� �����ؼ� Ÿ�̸� ���ͷ�Ʈ�� �ٽ� ����� �� �ֵ��� �Ѵ�.
            // ����, �����층�� ������ scheduler_flag �� false �� �����ؼ�,
            // Ÿ�̸� ���ͷ�Ʈ�� ���μ������� �ٽ� ����� �� �ֵ��� �Ѵ�.
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
    // 0���� ���μ��� ���� ���̶��
    else {
        // 0 ���μ��� �����ͼ�
        p = &(proc_tbl[run_proc0]);
        // 0 ���μ����� time_quantum �� 0���� �۰ų� ���ٸ�
        if (p->time_quantum <= 0) {
            // �غ� ���·� �����
            // ready_q�� ����
            p->state = PROCESS_READY;
            printf("%d process is turn to ready\n", p->id);
            Put_Tail_Q(&ready_q, p);
            Print_Q(&ready_q, TYPE_READY, 0);   // ���� ready_q �� �ִ� ���μ����� ���
            // ���� ready_q �� �ִ� ���μ��� �����ͼ�
            p = Get_Head_Q(&ready_q);
            // ���� ready_q�� �ƹ��͵� ���ٸ�,
            if (p == NULL) {
                // 0 ���μ��� ���� ������ �ʴٰ� ǥ���ϰ�,
                run_proc0 = -1;
                // nRun-- �ϱ�
                nRun--;
            }
            // ready_q �� �ִٸ�,
            else {
                // �ش� ���μ��� �����Ű��
                p->time_quantum = p->priority + 1;
                p->state = PROCESS_RUN;
                // ���μ��� 0�� ���������� ������ �̶��� id ���� �ȴ�.
                run_proc0 = p->id;
                printf("%d process is now run\n", p->id);
                if (which == ID_SLEEP)
                {
                    // sleep �� false �� �������ְ�, �̰� �ٽ� ��� �ٸ� ���Ǻ����� �˷��� �Ѵ�.
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
    // ���μ��� �����층 1���� �������� ���μ����� ���ٸ�
    if (run_proc1 == -1) {
        Print_Q(&ready_q, TYPE_READY, 1);   // ���� ready_q �� �ִ� ���μ����� ���
        p = Get_Head_Q(&ready_q);
        if (p != NULL) {
            p->time_quantum = p->priority + 1;
            p->state = PROCESS_RUN;
            run_proc1 = p->id;
            nRun++;
            printf("%d process is now run\n", p->id);
            if (which == ID_SLEEP)
            {
                // sleep �� false �� �������ְ�, �̰� �ٽ� ��� �ٸ� ���Ǻ����� �˷��� �Ѵ�.
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
    // ���� ���� ���μ����� �ִٸ�
    else {
        p = &(proc_tbl[run_proc1]);
        if (p->time_quantum <= 0) {
            p->state = PROCESS_READY;
            Put_Tail_Q(&ready_q, p);
            printf("%d process is turn to ready\n", p->id);
            Print_Q(&ready_q, TYPE_READY, 1);   // ���� ready_q �� �ִ� ���μ����� ���
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
                    // sleep �� false �� �������ְ�, �̰� �ٽ� ��� �ٸ� ���Ǻ����� �˷��� �Ѵ�.
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
    //std::unique_lock<std::mutex> lk(cv_m);
    if (!scheduler_flag.load())
    {
        // sleep �����층�� ���������� ���� ��쿡�� �����Ѵ�.
        //cv.wait(lk, [=] {return (!sleep_flag.load()); })
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
    Print_Q(&sleep_q, TYPE_SLEEP, -1);   // ���� sleep_q �� �ִ� ���μ����� ���
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
// os_item ���� ������ ����
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
// ������ ����
void Put_Tail_Item(os_item* head, os_item* item)
{
    (head->mu_lock).lock();
    item->prev = head->prev;
    head->prev->next = item;
    item->next = head;
    head->prev = item;
    (head->mu_lock).unlock();
}
// ������ ����
os_item* Produce_Item()
{
    os_item* item = new os_item();
    item->num = next_item.load();
    next_item.fetch_add(1);
    return item;
}
// ������ �Һ�
void Consume_Item(int item)
{
    item = 0;
}