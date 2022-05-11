//#include <iostream>	
//#include <thread>
//#include <atomic>	
//#include <Windows.h>
//#include <mutex>              // std::mutex, std::unique_lock
//#include <condition_variable>
//#include <vector>
//
//
//using namespace std;
//
//
//int a = 3;
//int b = 5;
//int tmp = 0;
//atomic<int> n{ 0 };
//int k = 0;
//
//void fn1()
//{
//	for (int i=0; i<100000; i++)
//	{
//		//n.store(1, memory_order_release);
//		n.fetch_add(1);
//		Sleep(100);
//	}
//}
//
//void fn2()
//{
//	//while (n.load(memory_order_acquire) == 0) 
//	//{
//	//	cout << "yes\n";
//	//}
//	for (int i = 0; i < 100000; i++)
//	{
//		k = n.load(memory_order_acquire);
//		cout << k << endl;
//		Sleep(100);
//	}
//}
//
//class MyClass
//{
//	int v;
//	int a;
//	int aarr[20];
//};
//
//bool readyFlag = false;
//std::mutex mtx;
//std::condition_variable cv;
//
//void waitFn()
//{
//	cout << "wait\n";
//	// wait 에는 unique_lock 이 필요하다
//	// unique_lock 밑에는 critical section 으로 보호된다.
//	// mtx 에 mutex_lock 을 걸고, while 문에서 readyFlag 를 읽는다.
//	std::unique_lock<std::mutex> lck(mtx);
//	// 이젠 notify 하면 공유변수도 값이 변경되었으므로, 이걸 검사한다.
//	// 공유 변수를 통해, notify 의 signal 이 씹히는 문제를 방지한다.
//	// 이러면 notify 가 wait 이전에 되었다고 해도, 공유변수의 값 변경된 걸 체크함으로써 notify 가 씹히지 않는다.
//	// readyFlag 값이 false 면, 계속 while 문을 반복하면서 대기한다.
//	while (!readyFlag)
//	{
//		// cv.wait 를 통해 이 스레드는 block 상태가 된다.
//		// 이후 다른 스레드로부터 notify 를 통해 signal 을 받으면, 그때 깨어난다.
//		// 이때 lock 이 걸려있던 뮤텍스 mtx 가 unlock 된다.
//		// 즉, 스레드가 block 되기 전에 뮤텍스를 unlock 하지 않으면 안 되기 때문에, unlock 시키는 것이다.
//		// 그리고 이게 뮤텍스를 unlock 해야, 뮤텍스를 사용하는 다른 스레드들이 해당 뮤텍스에 접근할 수 있다.
//		cv.wait(lck);
//		// 이후 signal 을 받으면, 
//		// 뮤텍스의 lock 을 한 후,
//		// while 문 안에서 깨어난 것이기 때문에, 다시 while 문에서 공유변수의 값을 검사한다.
//		// 검사조건을 완료하면, while 문을 빠져나오고,
//		// 이때 뮤텍스 lock 을 확보했으므로, 아래는 critical section 이 된다.
//	}
//	// 이렇게 람다식 형태로 사용할 수도 있다. true 를 반환하면 wait 이후 동작을 수행하도록 한다.
//	//cv.wait(lck, [] {return readyFlag; });
//	// while 문 아래, 여기 부분은 unique_lock 으로 보호되는 critical section 이다.
//	// 이렇게 명시적으로 unlock 시켜줘도 된다.
//	//lck.unlock();
//
//	cout << "re run\n";
//}
//
//void signalFn()
//{
//	cout << "signal\n";
//	// wait 씹히는 걸 방지하기 위한 공유변수 조작이다.
//	// 이때 이것도 뮤텍스 락으로 보호해준다.
//	{
//		// 여기 스코프 안에는 critical section 이 된다. 그리고 자동으로 스코프가 끝나면 unlock 이 된다.
//		std::lock_guard<std::mutex> lck(mtx);
//		readyFlag = true;
//	}
//	cv.notify_one();
//	// 이렇게 시그널을 모두에게 보내줄 수 있다.
//	// 물론 깨어난 스레드 중 뮤텍스를 lock 을 하고 그 이후 동작을 하는 건 하나의 스레드만 가능하다.
//	//cv.notify_all();
//}
//
//class StrStack
//{
//public:
//	// producer
//	void addStr(std::string s)
//	{
//		{
//			lock_guard<mutex> lck(mMtx);
//			mStrs.emplace_back(move(s));
//		}
//		mCv.notify_one();
//	}
//	// consumer
//	std::string getStr()
//	{
//		unique_lock<mutex> lck(mMtx);
//		while (mStrs.empty())
//		{
//			mCv.wait(lck);
//		}
//		string s = move(mStrs.back());
//		mStrs.pop_back();
//		lck.unlock();
//		return s;
//	}
//private:
//	vector<string> mStrs;
//	mutex mMtx;
//	condition_variable mCv;
//};
//
//
//
//
//int main()
//{
//	thread t1(fn1);
//	thread t2(fn2);
//	t1.join(); t2.join();
//
//
//	//thread waitT(waitFn);
//	//thread signalT(signalFn);
//	//waitT.join(); signalT.join();
//	
//	//StrStack strStack;
//
//	//thread t1([&]() {
//	//	strStack.addStr("nocope");
//	//	});
//	//thread t2([&]() {
//	//	strStack.addStr("moew");
//	//	});
//
//	//thread t3([&]() {
//	//	cout << strStack.getStr() << endl;
//	//	});
//
//	//thread t4([&]() {
//	//	cout << strStack.getStr() << endl;
//	//	});
//	//t1.join(); t2.join(); t3.join(); t4.join();
//
//
//}
//
//
//
//int main3()
//{
//	atomic<int> np;
//	atomic<MyClass> nc;
//	np.load();
//	np.load(memory_order_acquire);
//	cout << boolalpha << np.is_lock_free() << endl;
//	cout << boolalpha << nc.is_lock_free() << endl;
//	return 0;
//}