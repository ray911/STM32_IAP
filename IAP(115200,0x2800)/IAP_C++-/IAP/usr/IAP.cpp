#include "IAP.h"

IAP BootLoader(115200,FLASH_APP_ADDR,true);

void IAP::USART_init(u32 baud)
{
	GPIO_InitTypeDef GPIO_InitStructure;		
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;	
	
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1,ENABLE); 
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA ,ENABLE);
	
	
	
	GPIO_InitStructure.GPIO_Pin 	= GPIO_Pin_9;						
	GPIO_InitStructure.GPIO_Mode 	= GPIO_Mode_AF_PP;				
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;				
	GPIO_Init(GPIOA, &GPIO_InitStructure);									
	
	
	GPIO_InitStructure.GPIO_Pin 	= GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Mode 	= GPIO_Mode_IN_FLOATING;	
	GPIO_Init(GPIOA, &GPIO_InitStructure);						
	
	
	USART_InitStructure.USART_BaudRate=baud;																		
	USART_InitStructure.USART_WordLength=USART_WordLength_8b;													
	USART_InitStructure.USART_StopBits = USART_StopBits_1;													
	USART_InitStructure.USART_Parity = USART_Parity_No ; 														
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None; 	
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx; 							
	USART_Init(USART1, &USART_InitStructure);																				
	
	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);														
	USART_Cmd(USART1, ENABLE);															
	
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_0);	
	NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn; 		
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority= 0; 	
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;		
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;					
	NVIC_Init(&NVIC_InitStructure);				
}


IAP& IAP::operator<<(const char* pStr)
{
    while(*pStr)
    {
        USART1->DR= *pStr++;
		while((USART1->SR&0X40)==0);
		
    }
	return *this;
}

//构造函数
IAP::IAP(u32 baud,uint32_t startAddress,bool useHalfWord)
{
	USART_Data_Len=0;
	USART_COUNT=0;
	USART_FLAG=0;
	USART_init(baud);
	
	if(startAddress%STM_SECTOR_SIZE !=0)//不是页的开始,将开始处设置为下一个页开始的地方
		startAddress+=(STM_SECTOR_SIZE-(startAddress%STM_SECTOR_SIZE));
		mStartAddress=startAddress;
		mUseHalfWord=useHalfWord;
	
}

void IAP::USART_IRQ(void)
{
		unsigned char ch;
	if(USART_GetITStatus(USART1,USART_IT_RXNE) != RESET) {
		ch = USART_ReceiveData(USART1);
		USART_Buffer[USART_Data_Len++]=ch;
		USART_COUNT=0;//清空计数
		USART_FLAG=1;//标识读取
	}
}

void IAP::delay_ms(u16 nms)
{
	 u32 temp;
	 SysTick->LOAD = 9000*nms;
	 SysTick->VAL=0X00;
	 SysTick->CTRL=0X01;
	 do
	 {
	  temp=SysTick->CTRL;
	 }while((temp&0x01)&&(!(temp&(1<<16))));
	 SysTick->CTRL=0x00; 
	 SysTick->VAL =0X00; 
	
}

void USART1_IRQHandler(void)
{
	BootLoader.USART_IRQ();
}


//IAP***************************************************************************
void IAP::write_appbin()
{
	u16 t;
	u16 i=0;
	u16 buff[1024];
	u32 fwaddr=0;
	u16 temp;
	u8 *dfu=USART_Buffer;
	for(t=0;t<USART_Data_Len/2;t++)
	{						    
		//合成一个16位
		temp=(u16)dfu[1]<<8;
		temp+=(u16)dfu[0];	  
		dfu+=2;//偏移2个字节
		
		buff[i++]=temp;	
		if(i==STM_SECTOR_SIZE/2)
		{
			i=0;
			Write(fwaddr,buff,STM_SECTOR_SIZE/2);	
			fwaddr++;//翻到下一页
		}
	}
	if(i)Write(fwaddr,buff,i);//将最后的一些内容字节写进去.  
}


//跳转到应用程序段
//appxaddr:用户代码起始地址.
bool IAP::load_app()
{
	if(((*(vu32*)FLASH_APP_ADDR)&0x2FFE0000)==0x20000000)	//检查栈顶地址是否合法.
	{ 
		jump2app=(iapfun)*(vu32*)(FLASH_APP_ADDR+4);		//用户代码区第二个字为程序开始地址(复位地址)		
		MSR_MSP(*(vu32*)FLASH_APP_ADDR);					//初始化APP堆栈指针(用户代码区的第一个字用于存放栈顶地址)
		jump2app();											//跳转到APP.
		return true;
		
	}else{
		return false;
	}
}	

//Memory********************************************************************************************

bool IAP::Read(uint16_t pageNumber, uint16_t* data,u16 length)
{
	u16 dataLength=length;
	if(mUseHalfWord)
	{
		while(dataLength)
		{
			*data=(*(__IO uint16_t*)(mStartAddress+pageNumber*STM_SECTOR_SIZE+(length-dataLength)*2));
			++data;
			--dataLength;
		}
	}
	else
	{
		while(dataLength)
		{
			*data=(u32)(*(__IO uint32_t*)(mStartAddress+pageNumber*STM_SECTOR_SIZE+(length-dataLength)*4));
			++data;
			--dataLength;
		}
	}
	return true;
}


///////////////////////
///向储存器中特定位置写值
///@param -pageNumber 相对于开始地址的地址
///@param -Data 将要写入的数据
///@retval -1 : 写入成功 -0：写入失败
///////////////////////
bool IAP::Write (uint16_t pageNumber, uint16_t* data,u16 length)	
{
	u16 dataLength=length;
	FLASH_Unlock();
	FLASH_ClearFlag(FLASH_FLAG_BSY|FLASH_FLAG_EOP|
					FLASH_FLAG_PGERR|FLASH_FLAG_WRPRTERR);
	if(!FLASH_ErasePage(mStartAddress+pageNumber*STM_SECTOR_SIZE))//擦除页
		return false;
	if(mUseHalfWord)
	{
		while(dataLength)
		{
			if(FLASH_COMPLETE!=FLASH_ProgramHalfWord(mStartAddress+pageNumber*STM_SECTOR_SIZE+(length-dataLength)*2,*data))
				return false;
			++data;
			--dataLength;
		}
	}
	else
	{
		while(dataLength)
		{
			if(FLASH_COMPLETE!=FLASH_ProgramWord(mStartAddress+pageNumber*STM_SECTOR_SIZE+(length-dataLength)*4,(u32)*data))
				return false;
			++data;
			--dataLength;
		}
	}
	FLASH_Lock();
	return true;
}


extern "C"{
	
__asm void MSR_MSP(u32 addr) 
{
    MSR MSP, r0 			//set Main Stack value
    BX r14
}

};
