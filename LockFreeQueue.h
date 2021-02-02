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

			//*tempNodePtr�� nullptr�̶�� ���� ������ tail�̴�. ������ �´ٸ� ��ü
		} while ((LONG64)nullptr != InterlockedCompareExchange64((LONG64*)tempNodePtr, (LONG64)pNewNode, (LONG64)nullptr));

		// tail �ű��
		InterlockedCompareExchange128((LONG64*)&tail, (LONG64)tempNode.count + 1, (LONG64)pNewNode, (LONG64*)&tempNode);

		InterlockedIncrement64((LONG64*)&queueSize);

		InterlockedIncrement64((LONG64*)&enqueueCount);

		//���� �����̸� ���� ����!!!!
		// ù �ڵ� : do{pTempNode = tail;} while(CAS()) CAS();
		// ���⼭ �ι�° CAS�� �����ߴٰ� ���� -> tail�� nextNode�� nullptr�� �ɰ��̴�.
		// ���Ŀ� �ٸ� �����忡�� while()�� �����Ѵٰ� ������ ��� CAS�� ù��° �Ű������� tail->pNextNode�� nullptr�� �ɰ��̰�, �����ϰԵȴ�.
		// �׷��� pTempNode = tail(���Ű�)���� �ι�° CAS�� �����ϱ⶧���� �ι�° CAS�� �����ϰԵȴ�.
		//InterlockedCompareExchange64((LONG64*)&tail, (LONG64)pNewNode, (LONG64)pTempNode);
		// �����ϰԵǸ�, tail�� next�� nullptr�� �ٲ��� �ʱ⶧���� ��� �Լ� ȣ���� �����ϰԵȴ�.
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


			//ī��Ʈ�� �̸� �޾Ƴ��´�.
			headTemp.count = head.count;
			headTemp.nodePtr = head.nodePtr;

			tailTemp.count = tail.count;
			tailTemp.nodePtr = tail.nodePtr;

			headNextTempPtr = ((stNODE*)headTemp.nodePtr)->pNextNode;
			tailNextTempPtr = ((stNODE*)tailTemp.nodePtr)->pNextNode;

			// head->next �ӽ÷� ����
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

			//���� ���ǹ��� ����ϴ���, ���Ͷ����� ī��Ʈ�� �޶����� ó���� Pop�� ������������, �ι�° ī��Ʈ�� ���ÿ� �ִ� ī��Ʈ�� ����
			//�޶����� ���Ͷ��� �����Ѵ�. �׷��� �ٽ� �õ��ϴµ�, ���� �̶��� Push�� ���� �ʾҴٸ�, if������ �ɸ��Եǰ� return false;�ϰԵȴ�.
			//nullptr�� �հ�͵� ���� ���Ͷ����� ī��Ʈ�� �޶����⶧���� �����ϰ� �ٽ� �õ��ϰԵȴ�.
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