#pragma once

#include "CMemoryPoolTLS/CMemoryPool.h"

template <class DATA>
class LockFreeQueue
{
private:
	__declspec(align(16))struct INT128
	{
		UINT64 nodePtr;
		UINT64 count;
	};
	struct stNODE
	{
		stNODE()
		{
			pNextNode = nullptr;
		}
		DATA data;
		stNODE* pNextNode;
	};

	INT128 tail;
	INT128 head;

	CMemoryPool<stNODE> memoryPool;

	LONG64 enqueueCount;
	LONG64 dequeueCount;

	LONG64 queueSize;
public:
	LockFreeQueue()
		: enqueueCount(0), dequeueCount(0), memoryPool(1, false)
	{
		stNODE* pNode = memoryPool.Alloc();

		INT* ptr = (INT*)((char*)pNode + sizeof(stNODE));

		pNode->pNextNode = nullptr;

		tail.nodePtr = (UINT64)pNode;
		tail.count = 0;

		head.nodePtr = (UINT64)pNode;
		head.count = 0;
	}

	void Enqueue(DATA data)
	{
		stNODE* pNewNode = memoryPool.Alloc();

		pNewNode->data = data;
		pNewNode->pNextNode = nullptr;

		INT128 tempNode = { 0, };
		stNODE** tempNodePtr = nullptr;

		do
		{
		PUSH_FIRST:
			tempNode.count = tail.count;
			tempNode.nodePtr = tail.nodePtr;

			tempNodePtr = &(((stNODE*)tempNode.nodePtr)->pNextNode);

			if (*tempNodePtr != nullptr)
			{
				InterlockedCompareExchange128((LONG64*)&tail, (LONG64)tempNode.count + 1, (LONG64)*tempNodePtr, (LONG64*)&tempNode);

				goto PUSH_FIRST;
			}

			//*tempNodePtr이 nullptr이라면 가장 마지막 tail이다. 마지막 맞다면 교체
		} while ((LONG64)nullptr != InterlockedCompareExchange64((LONG64*)tempNodePtr, (LONG64)pNewNode, (LONG64)nullptr));

		// tail 옮기기
		InterlockedCompareExchange128((LONG64*)&tail, (LONG64)tempNode.count + 1, (LONG64)pNewNode, (LONG64*)&tempNode);

		InterlockedIncrement64((LONG64*)&queueSize);

		InterlockedIncrement64((LONG64*)&enqueueCount);

		//무한 뺑뱅이를 도는 이유!!!!
		// 첫 코드 : do{pTempNode = tail;} while(CAS()) CAS();
		// 여기서 두번째 CAS가 성공했다고 가정 -> tail의 nextNode는 nullptr이 될것이다.
		// 직후에 다른 스레드에서 while()을 실행한다고 가정할 경우 CAS의 첫번째 매개변수인 tail->pNextNode는 nullptr이 될것이고, 성공하게된다.
		// 그러나 pTempNode = tail(과거값)으로 두번째 CAS를 진행하기때문에 두번째 CAS는 실패하게된다.
		//InterlockedCompareExchange64((LONG64*)&tail, (LONG64)pNewNode, (LONG64)pTempNode);
		// 실패하게되면, tail의 next가 nullptr로 바뀌지 않기때문에 모든 함수 호출이 실패하게된다.
	}

	bool Dequeue(DATA* pData)
	{
		INT128 headTemp;

		INT128 tailTemp;

		stNODE* headNextTempPtr;
		stNODE* tailNextTempPtr;

		DATA tempData;

		do
		{
		POP_FIRST:
			if (queueSize == 0)
			{
				return false;
			}


			//카운트를 미리 받아놓는다.
			headTemp.count = head.count;
			headTemp.nodePtr = head.nodePtr;

			tailTemp.count = tail.count;
			tailTemp.nodePtr = tail.nodePtr;

			headNextTempPtr = ((stNODE*)headTemp.nodePtr)->pNextNode;
			tailNextTempPtr = ((stNODE*)tailTemp.nodePtr)->pNextNode;

			// head->next 임시로 복사
			if (headNextTempPtr == nullptr)
			{
				goto POP_FIRST;
			}
			
			if (tailNextTempPtr != nullptr)
			{
				InterlockedCompareExchange128((LONG64*)&tail, (LONG64)tailTemp.count + 1, (LONG64)tailNextTempPtr, (LONG64*)&tailTemp);
				goto POP_FIRST;
			}

			tempData = headNextTempPtr->data;

			//위의 조건문을 통과하더라도, 인터락에서 카운트가 달라져서 처음의 Pop은 성공할지언정, 두번째 카운트는 로컬에 있는 카운트랑 값이
			//달라져서 인터락은 실패한다. 그래서 다시 시도하는데, 만약 이때도 Push를 하지 않았다면, if문에서 걸리게되고 return false;하게된다.
			//nullptr을 뚫고와도 밑의 인터락에서 카운트가 달라지기때문에 실패하고 다시 시도하게된다.
		} while (!InterlockedCompareExchange128((LONG64*)&head, (LONG64)headTemp.count + 1, (LONG64)headNextTempPtr, (LONG64*)&headTemp));

		*pData = tempData;

		memoryPool.Free((stNODE*)headTemp.nodePtr);

		InterlockedDecrement64((LONG64*)&queueSize);

		InterlockedIncrement64((LONG64*)&dequeueCount);

		return true;
	}

	int GetQueueSize()
	{
		return queueSize;
	}
};