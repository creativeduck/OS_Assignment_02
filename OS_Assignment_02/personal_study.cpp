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
//	// wait ���� unique_lock �� �ʿ��ϴ�
//	// unique_lock �ؿ��� critical section ���� ��ȣ�ȴ�.
//	// mtx �� mutex_lock �� �ɰ�, while ������ readyFlag �� �д´�.
//	std::unique_lock<std::mutex> lck(mtx);
//	// ���� notify �ϸ� ���������� ���� ����Ǿ����Ƿ�, �̰� �˻��Ѵ�.
//	// ���� ������ ����, notify �� signal �� ������ ������ �����Ѵ�.
//	// �̷��� notify �� wait ������ �Ǿ��ٰ� �ص�, ���������� �� ����� �� üũ�����ν� notify �� ������ �ʴ´�.
//	// readyFlag ���� false ��, ��� while ���� �ݺ��ϸ鼭 ����Ѵ�.
//	while (!readyFlag)
//	{
//		// cv.wait �� ���� �� ������� block ���°� �ȴ�.
//		// ���� �ٸ� ������κ��� notify �� ���� signal �� ������, �׶� �����.
//		// �̶� lock �� �ɷ��ִ� ���ؽ� mtx �� unlock �ȴ�.
//		// ��, �����尡 block �Ǳ� ���� ���ؽ��� unlock ���� ������ �� �Ǳ� ������, unlock ��Ű�� ���̴�.
//		// �׸��� �̰� ���ؽ��� unlock �ؾ�, ���ؽ��� ����ϴ� �ٸ� ��������� �ش� ���ؽ��� ������ �� �ִ�.
//		cv.wait(lck);
//		// ���� signal �� ������, 
//		// ���ؽ��� lock �� �� ��,
//		// while �� �ȿ��� ��� ���̱� ������, �ٽ� while ������ ���������� ���� �˻��Ѵ�.
//		// �˻������� �Ϸ��ϸ�, while ���� ����������,
//		// �̶� ���ؽ� lock �� Ȯ�������Ƿ�, �Ʒ��� critical section �� �ȴ�.
//	}
//	// �̷��� ���ٽ� ���·� ����� ���� �ִ�. true �� ��ȯ�ϸ� wait ���� ������ �����ϵ��� �Ѵ�.
//	//cv.wait(lck, [] {return readyFlag; });
//	// while �� �Ʒ�, ���� �κ��� unique_lock ���� ��ȣ�Ǵ� critical section �̴�.
//	// �̷��� ��������� unlock �����൵ �ȴ�.
//	//lck.unlock();
//
//	cout << "re run\n";
//}
//
//void signalFn()
//{
//	cout << "signal\n";
//	// wait ������ �� �����ϱ� ���� �������� �����̴�.
//	// �̶� �̰͵� ���ؽ� ������ ��ȣ���ش�.
//	{
//		// ���� ������ �ȿ��� critical section �� �ȴ�. �׸��� �ڵ����� �������� ������ unlock �� �ȴ�.
//		std::lock_guard<std::mutex> lck(mtx);
//		readyFlag = true;
//	}
//	cv.notify_one();
//	// �̷��� �ñ׳��� ��ο��� ������ �� �ִ�.
//	// ���� ��� ������ �� ���ؽ��� lock �� �ϰ� �� ���� ������ �ϴ� �� �ϳ��� �����常 �����ϴ�.
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