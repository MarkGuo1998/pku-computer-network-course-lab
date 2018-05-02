#include "sysinclude.h"
#include <winsock2.h>
#include <queue>
#include <iostream>
#include <stdio.h>
using namespace std;

extern void SendFRAMEPacket(unsigned char* pData, unsigned int len);

#define WINDOW_SIZE_STOP_WAIT 1
#define WINDOW_SIZE_BACK_N_FRAME 4

typedef enum { data, ack, nak } frame_kind;
typedef struct frame_head
{
	frame_kind kind;
	unsigned seq;
	unsigned ack;
	unsigned char data[100];
};
typedef struct frame
{
	frame_head head;
	unsigned size;
};
typedef struct buffer
{
	frame data;
	int size;
	buffer(char* data_ = "0", int size_ = 0) : data(*(frame*)data_), size(size_) {};
};

#define QUEUE_MAX_LENGTH 10000


/*
* 停等协议测试函数
*/
int stud_slide_window_stop_and_wait(char *pBuffer, int bufferSize, UINT8 messageType)
{
	static queue<buffer> sendQueue;
	static int windowNumber = 0;
	if (messageType == MSG_TYPE_SEND)
	{
		buffer tmp(pBuffer, bufferSize);

		sendQueue.push(tmp);

		if (windowNumber < WINDOW_SIZE_STOP_WAIT)
		{
			windowNumber++;
			buffer now = sendQueue.front();
			SendFRAMEPacket((unsigned char*)&now.data, now.size);
		}
	}
	else if (messageType == MSG_TYPE_RECEIVE)
	{
		windowNumber--;
		sendQueue.pop();
		if (!sendQueue.empty())
		{
			windowNumber++;
			buffer now = sendQueue.front();
			SendFRAMEPacket((unsigned char*)&now.data, now.size);
		}
	}
	else if (messageType == MSG_TYPE_TIMEOUT)
	{
		buffer now = sendQueue.front();
		SendFRAMEPacket((unsigned char*)&now.data, now.size);
	}
	return 0;
}

/*
* 回退n帧测试函数
*/
int stud_slide_window_back_n_frame(char *pBuffer, int bufferSize, UINT8 messageType)
{
	static buffer sendQueue[QUEUE_MAX_LENGTH];
	static int head = 1, tail = 1;
	static int windowHead = 1; //windowTail = head

	if (messageType == MSG_TYPE_SEND)
	{
		buffer tmp(pBuffer, bufferSize);

		sendQueue[tail++] = tmp;

		if (head - windowHead < WINDOW_SIZE_BACK_N_FRAME)
		{
			buffer now = sendQueue[head++];
			SendFRAMEPacket((unsigned char*)&now.data, now.size);
		}
	}
	else if (messageType == MSG_TYPE_RECEIVE)
	{
		buffer tmp(pBuffer, bufferSize);

		while (head - windowHead > 0 && sendQueue[windowHead].data.head.seq != tmp.data.head.ack)
		{
			windowHead++;
			if (head < tail)
			{
				buffer now = sendQueue[head++];
				SendFRAMEPacket((unsigned char*)&now.data, now.size);
			}
		}
		windowHead++;
		if (head < tail)
		{
			buffer now = sendQueue[head++];
			SendFRAMEPacket((unsigned char*)&now.data, now.size);
		}

	}
	else if (messageType == MSG_TYPE_TIMEOUT)
	{
		for (int i = windowHead; i < head; i++)
		{
			buffer now = sendQueue[i];
			SendFRAMEPacket((unsigned char*)&now.data, now.size);
		}

	}
	return 0;
}

/*
* 选择性重传测试函数
*/
int stud_slide_window_choice_frame_resend(char *pBuffer, int bufferSize, UINT8 messageType)
{
	static buffer sendQueue[QUEUE_MAX_LENGTH];
	static int head = 1, tail = 1;
	static int windowHead = 1; //windowTail = head

	if (messageType == MSG_TYPE_SEND)
	{
		buffer tmp(pBuffer, bufferSize);

		sendQueue[tail++] = tmp;

		if (head - windowHead < WINDOW_SIZE_BACK_N_FRAME)
		{
			buffer now = sendQueue[head++];
			SendFRAMEPacket((unsigned char*)&now.data, now.size);
		}
	}
	else if (messageType == MSG_TYPE_RECEIVE)
	{
		buffer tmp(pBuffer, bufferSize);
		if (ntohl(tmp.data.head.kind) == ack)
		{
			while (head - windowHead > 0 && sendQueue[windowHead].data.head.seq != tmp.data.head.ack)
			{
				windowHead++;
				if (head < tail)
				{
					buffer now = sendQueue[head++];
					SendFRAMEPacket((unsigned char*)&now.data, now.size);
				}
			}
			windowHead++;
			if (head < tail)
			{
				buffer now = sendQueue[head++];
				SendFRAMEPacket((unsigned char*)&now.data, now.size);
			}
		}
		else
		{
			for (int i = windowHead; i < head; i++)
			{
				buffer now = sendQueue[i];
				if (now.data.head.seq == tmp.data.head.ack)
				{
					SendFRAMEPacket((unsigned char*)&now.data, now.size);
					break;
				}
			}
		}
	}
	return 0;
}
