#include "sysinclude.h"
// #include <winsock2.h>
#include <queue>
#include <iostream>
#include <stdio.h>
using namespace std;

extern void SendFRAMEPacket(unsigned char* pData, unsigned int len);

#define WINDOW_SIZE_STOP_WAIT 1
#define WINDOW_SIZE_BACK_N_FRAME 4

/* 
 * 下面是按照题目给定的帧数据结构，构造出的结构体；我为了队列操作的方便，额外写了一个默认构造函数。
 * 缓存的必须是帧（而非帧的地址）。存地址会带来严重的问题，后发的帧可能冲掉前面的帧。
 */
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

// 假设输入缓冲区的队列长度为10000
#define QUEUE_MAX_LENGTH 10000


/* 
 * 停等协议
 */
int stud_slide_window_stop_and_wait(char *pBuffer, int bufferSize, UINT8 messageType)
{
	// 这里为了方便，直接用了STL的队列。
	static queue<buffer> sendQueue;
	static int windowNumber = 0;
	
	// 分三种情况讨论，第一种是需要发送一个帧。
	if (messageType == MSG_TYPE_SEND)
	{
		// 使用默认构造函数把pBuffer指向的bufferSize个字节缓存起来，并强制转换为frame格式。
  		// 这里也可以使用memcpy函数，但是在后面的任务里要用到ack和seq号（需用ntoh进行转换）。
		// 那时用memcpy就比较麻烦了。为了统一，我还是使用了结构体的方法。
		buffer tmp(pBuffer, bufferSize);
		sendQueue.push(tmp);

		// 如果窗口数没达到上限（1），开一个新窗口，并发送队列里的第一帧。
		// 发送这一帧不意味着将其pop出队！因为可能超时重发，若pop出去，就找不到这一帧了。
		// 正确的做法是等ACK（RECEIVE）时再出队。
		if (windowNumber < WINDOW_SIZE_STOP_WAIT)
		{
			windowNumber++;
			buffer now = sendQueue.front();
			SendFRAMEPacket((unsigned char*)&now.data, now.size);
		}
	}
	
	// 第二种情况是收到一个ACK。我们不必额外验证ack号。
	// 因为停等协议的发端每次只发一帧，所以收端发回的ACK一定意味着“上一帧正确发送”。
	else if (messageType == MSG_TYPE_RECEIVE)
	{
  		// 确认成功发送一帧，同时关闭一个窗口；根据第一种情况的讨论，需要让一个帧出队。
		windowNumber--;
		sendQueue.pop();
		
		// 此时如果队列里有待发送的帧，将其发送。仍旧是不pop。
		if (!sendQueue.empty())
		{
			windowNumber++;
			buffer now = sendQueue.front();
			SendFRAMEPacket((unsigned char*)&now.data, now.size);
		}
	}
	
	// 第三种情况，超时。
	// 由于之前没pop，故超时帧就是当前队列第一帧，直接重发即可。
	else if (messageType == MSG_TYPE_TIMEOUT)
	{
		buffer now = sendQueue.front();
		SendFRAMEPacket((unsigned char*)&now.data, now.size);
	}
	return 0;
}

/*
 * 回退N帧协议
 */
int stud_slide_window_back_n_frame(char *pBuffer, int bufferSize, UINT8 messageType)
{
	// 根据报告里的讨论，这里要手写队列sendQueue。
	// head和tail分别是缓冲队列的头指针和尾指针。
	// head代表当前队首元素的位置，tail表示下一个入队元素存储的位置。
	// 因此(head == tail)表示队列为空。
	// windowHead表示当前窗口中（已发送）的第一帧，windowTail表示下一个要进入窗口的帧。
	// 注意到windowTail就是head。
	static buffer sendQueue[QUEUE_MAX_LENGTH];
	static int head = 1, tail = 1;
	static int windowHead = 1; 

	// 第一种情况，发送帧。
	if (messageType == MSG_TYPE_SEND)
	{
		buffer tmp(pBuffer, bufferSize);
		sendQueue[tail++] = tmp;
		
		// 窗口未满，发送一帧。
		// 这里前面已经有一帧入队，所以队列一定非空，不必额外检查。
		if (head - windowHead < WINDOW_SIZE_BACK_N_FRAME)
		{
			buffer now = sendQueue[head++];
			SendFRAMEPacket((unsigned char*)&now.data, now.size);
		}
	}
	
	// 第二种情况，接收帧。
	else if (messageType == MSG_TYPE_RECEIVE)
	{
		buffer tmp(pBuffer, bufferSize);
		
 		// 由于使用累计确认，因此将窗口一直往后滑，直到找到ack对应的那帧。
 		// 那一帧之前的所有帧，都正确发送了。
		while (head - windowHead > 0 
			&& sendQueue[windowHead].data.head.seq != tmp.data.head.ack)
		{
			windowHead++;
			
			// 每滑一次窗口，都要从队列头取出并发送新的一帧。这里必须判断队列是否空。
			if (head < tail)
			{
				buffer now = sendQueue[head++];
				SendFRAMEPacket((unsigned char*)&now.data, now.size);
			}
		}
		
 		// 现在windowHead指向正确发送的最后一帧。
 		// 但那一帧本身也正确发送了，因此需要再滑一次
		windowHead++;
		if (head < tail)
		{
			buffer now = sendQueue[head++];
			SendFRAMEPacket((unsigned char*)&now.data, now.size);
		}
	}
	
	// 第三种情况，超时。
	else if (messageType == MSG_TYPE_TIMEOUT)
	{
		// 从第二种情况得知，一旦超时，则windowHead指向“未正确发送的第一帧”。
		// 因此将该帧及之后窗口里的所有帧重发。
		for (int i = windowHead; i < head; i++)
		{
			buffer now = sendQueue[i];
			SendFRAMEPacket((unsigned char*)&now.data, now.size);
		}
	}
	return 0;
}

/*
 * 选择重传协议
 */
int stud_slide_window_choice_frame_resend(char *pBuffer, int bufferSize, UINT8 messageType)
{
	// 和回退N协议一样
	static buffer sendQueue[QUEUE_MAX_LENGTH];
	static int head = 1, tail = 1;
	static int windowHead = 1; 

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
	
	// 这里没有超时，只有“收到”的ACK/NAK。分情况讨论。
	else if (messageType == MSG_TYPE_RECEIVE)
	{
		buffer tmp(pBuffer, bufferSize);
		
		// 第一种情况，ACK。（kind变量是一个枚举类型，所以务必注意大小端！）
		if (ntohl(tmp.data.head.kind) == ack)
		{
			// 和回退N协议一样。
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
		
		// 第二种情况，NAK。
		else
		{
			for (int i = windowHead; i < head; i++)
			{
				buffer now = sendQueue[i];
				// 遍历并找到nak对应的帧序号，重发该帧。
				// 这里两个变量都是network格式，就不必调用ntoh了。
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