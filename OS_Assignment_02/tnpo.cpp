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
#define ID_PRODUCER 1    // ������ ID
#define ID_CONSUMER 2    // �Һ��� ID

#define FAVOUR_TIMER 3 // Ÿ�̸� ���ͷ�Ʈ ����
#define FAVOUR_SLEEP 4 // sleep ����
#define ID_TIMER 3     // Ÿ�̸� ���ͷ�Ʈ�� ���� �����층���� ����    
#define ID_SLEEP 4     // sleep ���� ���� �����층���� ����


struct os_item {
    int num;		// ������ ��ȣ
    std::mutex mu_lock;
    struct os_item* prev;
    struct os_item* next;
};
// ������ - �Һ��� ����
struct os_item os_item_q;			// ������ �ְ� ���� ť
std::atomic<int> item_count{ 0 };		// ������ ����
std::atomic<int> dekker_favoured{ FAVOUR_PRODUCER };	// dekker �˰���� turn
std::atomic<bool> dekker_producer{ false };	// producer�� dekker �˰���� ����
std::atomic<bool> dekker_consumer{ false };	// consumer�� dekker �˰���� ����
// ������ ������ �ε��� ���� ������ ����
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
void Consume_Item(os_item* item);

void proc_timer_int(int id)    /* Process 0: Timer Interrupt Genrator */
{
    proc_tbl_t* p;
    int sched_required;

    Sleep(100);
    for (;;) {
        // �����층�� �������� ��� scheduler_flag �� true �Դϴ�.
        // ���μ����� sleep �Ǵ� ���, sleep_flag �� true�Դϴ�.
        // ���� �����층�� ���������� �ʰ�, ���� sleep �Ǵ� ���μ����� ���� ���,
        // Ÿ�̸� ���ͷ�Ʈ�� ����� �� �ֵ��� �߽��ϴ�.
        // �̸� ���� �����층�� �̹� ����ǰ� �ִµ� Ÿ�̸� ���ͷ�Ʈ�� ���� �� �����층�� �ǰų�,
        // �Ǵ� ���μ����� sleep �ǰ� �ִµ� Ÿ�̸� ���ͷ�Ʈ�� �����Ͽ� �ߺ����� �����층�Ǵ� �� �����߽��ϴ�.
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
                // Ÿ�̸� ���ͷ�Ʈ�� ���� ���� ��Ȳ����, 
                // Ÿ�̸� ���ͷ�Ʈ�� atomic �ϰ� ������� �ʱ� ������
                // �����층�� �ϱ� ���� �� ������ �ٽ� üũ�ؼ� 
                // ���� �����층�� ���������� �ʰ�, sleep �Ǵ� ���μ����� ���� ��쿡�� �����층�� �����մϴ�.
                if (!scheduler_flag.load() && !sleep_flag.load())
                {
                    printf("Scheduled from timer\n");
                    // �����층�� ������ ���̹Ƿ�, �ش� ������ true�� �����մϴ�.
                    scheduler_flag.store(true);
                    // TIMER �κ����� �����층���� �����ϸ鼭, �����층�մϴ�.
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
        // �����층�� ���������� ���� ��Ȳ������ ���μ����� �����ϵ��� �մϴ�.
        // �̸� ����, �� ���μ����� ����Ǹ鼭 sleep �Ǵ� ���μ����� �߻��ϴ� �� �����մϴ�.
        // ���� �����층�� ����ǰ� �ִ� ���¿���, sleep �Ǵ� ���μ����� �߻��ϸ�,
        // �̷����� �ߺ� �����층�� �߻��� ���α׷��� �ǵ���� �������� �ʾҽ��ϴ�.
        cv.wait(lk, [=] {return (!scheduler_flag.load() && p->state == PROCESS_RUN); });
        // ��ȣ���� �κ�
        // dekker �˰����� ����� �����߽��ϴ�.
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
        // ������ �ִ� ������ �ʰ��ϸ�, ���ϴ�.
        if (item_count.load() >= MAX_ITEM)
        {
            // �̶� sleep_flag �� true �� �����ؼ�, ���μ����� sleep �Ǿ�� �Ѵٰ� �˸��ϴ�.
            sleep_flag.store(true);
            printf("\nNo more item can be produced\n");
            // ��ȣ���� ������ �������մϴ�.
            dekker_favoured.store(FAVOUR_CONSUMER);
            dekker_producer.store(false);
            // ������ ���μ����� sleep �Ǿ�� ���� �����ϸ鼭, syscall_sleep �Լ��� ȣ���մϴ�.
            syscall_sleep(ID_PRODUCER);
        }
        else
        {
            // �׷��� �ʴٸ�, ������ ť �� �ڿ� �������� �����մϴ�.
            Put_Tail_Item(&os_item_q, item);
            item_count.fetch_add(1);  // ������ ������ 1 ������ŵ�ϴ�.
            printf("Item Inserted / Total Item : %d items \n", item_count.load());
            // ������ �ϳ��� ������, �Һ��� ���μ����� ����ϴ�.
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
        // �����층�� ���������� ���� ��Ȳ������ ���μ����� �����ϵ��� �մϴ�.
        cv.wait(lk, [=] {return (!scheduler_flag.load() && p->state == PROCESS_RUN); });
        // dekker �˰����� ����� ��ȣ������ �޼��մϴ�.
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
        // ���� ������ ������ 0�̸�, �Һ��� ���μ����� ���ϴ�.
        if (item_count.load() == 0)
        {
            sleep_flag.store(true); // sleep_flag ������ true�� �����մϴ�.
            printf("\nNo more item can be consumed\n");
            // ��ȣ���� �κ�
            dekker_favoured.store(FAVOUR_PRODUCER);
            dekker_consumer.store(false);
            syscall_sleep(ID_CONSUMER);
        }
        else
        {
            // �׷��� �ʴٸ�, �������� �ϳ� �����ɴϴ�.
            item = Get_Head_Item(&os_item_q);
            item_count.fetch_sub(1); // ������ ������ �ϳ� ���ҽ�ŵ�ϴ�.
            printf("\nConsume Item / %d items left\n", item_count.load());
            // ������ ������ �� ���� �ʰ� �Ǹ�, �����ڸ� ����ϴ�.
            if (item_count.load() == MAX_ITEM - 1)
                syscall_wakeup();
            Consume_Item(item);
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
        // �����층�� ���������� ���� ��Ȳ������ ���μ����� �����ϵ��� �մϴ�.
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
        // �����층�� ���������� ���� ��Ȳ������ ���μ����� �����ϵ��� �մϴ�.
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
            // sleep_flag �� false �� �����ؼ� Ÿ�̸� ���ͷ�Ʈ�� �ٽ� ����� �� �ֵ��� �մϴ�.
            // ����, �����층�� ������ scheduler_flag �� false �� �����ؼ�,
            // Ÿ�̸� ���ͷ�Ʈ�� ���μ������� �ٽ� ����� �� �ֵ��� �մϴ�.
            // �Ʒ����� ������ ������ �ݺ��̹Ƿ�, ������ ���⼭�� �ϰڽ��ϴ�.
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
            Print_Q(&ready_q, TYPE_READY, 0);   // ���� ready_q �� �ִ� ���μ����� ���
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
    // �����층�� ���������� ���� ��쿡�� ���μ����� sleep �մϴ�.
    // �̴� Ÿ�̸� ���ͷ�Ʈ�� �������� ��Ȳ���� sleep �� ��û�� ���,
    // sleep �� �����ϴ� �뵵�� ����߽��ϴ�.
    // Ÿ�̸� ���ͷ�Ʈ�� ���� �����층�� �߻��ϹǷ�,
    // sleep �Ǿ�� �ϴ� ���μ����� ������ �������� �����ϰų� �Һ����� ���ϸ�,
    // Ÿ�̸� ���ͷ�Ʈ�� ���� ���� ������ �ش� ���μ����� ����ִٸ�,
    // �׶� sleep �ϵ��� �ؼ� �ִ��� ���α׷��� �ǵ��� ��� �����ϵ��� �߽��ϴ�.
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
// ���� ready_q���� ���μ����� �������� �Ͱ� ������ �����Դϴ�.
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
// ���� ready_q�� ���μ����� �����ϴ� �Ͱ� ������ �����Դϴ�.
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
// �ܼ��� item �� ���ڸ� 1�� �����ϸ鼭 �������� �����߽��ϴ�.
os_item* Produce_Item()
{
    os_item* item = new os_item();
    item->num = next_item.load();
    next_item.fetch_add(1);
    return item;
}
// ������ �Һ�
// �ܼ��� �ش� �������� NULL�� �߽��ϴ�..
void Consume_Item(os_item* item)
{
    item = NULL;
}