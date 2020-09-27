#define test_crc 1
#if !test_crc
#include "server.h"
#endif 
#include <stdio.h>
#include <stdlib.h>
/*
 * Copyright 2001-2010 Georges Menie (www.menie.org)
 * Copyright 2010-2012 Salvatore Sanfilippo (adapted to Redis coding style)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the University of California, Berkeley nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* CRC16 implementation according to CCITT standards.
 *
 * Note by @antirez: this is actually the XMODEM CRC 16 algorithm, using the
 * following parameters:
 *
 * Name                       : "XMODEM", also known as "ZMODEM", "CRC-16/ACORN"
 * Width                      : 16 bit
 * Poly                       : 1021 (That is actually x^16 + x^12 + x^5 + 1)  ==>Poly_src 1 0001 0000 0010 0001 
 * Initialization             : 0000
 * Reflect Input byte         : False
 * Reflect Output CRC         : False
 * Xor constant to output CRC : 0000
 * Output for "123456789"     : 31C3
 */
// 16 => 0001 0000
// 12 => 0000 1100
//  5 => 0000 0101  ----> 0000 0001
/**
 *    CRC编码的基本思想是：将二进制位串的操作被解释为多项式算法运算。
 * 二进制数据可以看作是一个k-1 次多项式的系数列表，该多项式共有k项，从x^(k-1)到x^0。这样的多项式被认为是k-1阶多项式。高次(左边)位是x^(k-1)项的系数，接下来的位是x(k-2)项的系数，以此类推。例如： 100101有6位，因此代表了一个有6项的多项式，其系数分别是1、0、0、1、0和1, 即1x^5 + 0x^4 + 0x^3 + 1x^2 + 0x^1 + 1x^0 == x^5 + x^2 + 1。
 * 
 * 几种CRC16计算公式、初始值、标志位等参数汇总
 * 一、CRC16/IBM 或 CRC16/ARC 或 CRC16/LHA：

    公式：x16+x15+x2+1

    宽度：16

    Poly值：0x8005

    初始值：0x0000

    基准输入：true

    基准输出：true

    标志位：0x0000

 

 

二、CRC16/MAXIM：

    公式：x16+x15+x2+1

    宽度：16

    Poly值：0x8005

    初始值：0x0000

    基准输入：true

    基准输出：true

    标志位：0xFFFF

 

三、CRC16/USB：

    公式：x16+x15+x2+1

    宽度：16

    Poly值：0x8005

    初始值：0xFFFF

    基准输入：true

    基准输出：true

    标志位：0xFFFF

 

四、CRC16/MODBUS(最常见)：

    公式：x16+x15+x2+1

    宽度：16

    Poly值：0x8005

    初始值：0x0000

    基准输入：true

    基准输出：true

    标志位：0x0000

 

五、CRC16/CCITT 或 CRC-CCITT 或CRC16/CCITT-TRUE或 CRC16/KERMIT：

    公式：x16+x15+x5+1

    宽度：16

    Poly值：0x1021

    初始值：0x0000

    基准输入：true

    基准输出：true

    标志位：0x0000

 

六、 CRC16/CCITT-FALSE：

    公式：x16+x15+x5+1

    宽度：16

    Poly值：0x1021

    初始值：0xFFFF

    基准输入：false

    基准输出：false

    标志位：0x0000

 

七、CRC16/X25：

    公式：x16+x15+x5+1

    宽度：16

    Poly值：0x1021

    初始值：0x0000

    基准输入：true

    基准输出：true

    标志位：0xFFFF

 

八、CRC16/XMODEM 或 CRC16/ZMODEM 或 CRC16/ACORN：

    公式：x16+x15+x5+1

    宽度：16

    Poly值：0x1021

    初始值：0x0000

    基准输入：false

    基准输出：false

    标志位：0x0000

 

九、CRC16/DNP：

    公式：x16+x13+x12+x11+x10+x8+x6+x5+x2+1

    宽度：16

    Poly值：0x3D65

    初始值：0x0000

    基准输入：true

    基准输出：true

    标志位：0xFFFF

   使用：M-Bus, ect

 

 

附加其它：

 

一、CRC4/ITU：

    公式：x4+x+1

    宽度：4

    Poly值：0x03

    初始值：0x00

    基准输入：true

    基准输出：true

    标志位：0x00

 

 

二、CRC5/EPC：

    公式：x5+x3+1

    宽度：5

    Poly值：0x09

    初始值：0x09

    基准输入：false

    基准输出：false

    标志位：0x00

 

 

三、CRC5/ITU：

    公式：x5+x4+x2+1

    宽度：5

    Poly值：0x15

    初始值：0x00

    基准输入：true

    基准输出：true

    标志位：0x00

 

 

四、CRC5/USB：

    公式：x5+x2+1

    宽度：5

    Poly值：0x05

    初始值：0x1F

    基准输入：true

    基准输出：true

    标志位：0x1F

 

 

四、CRC6/ITU：

    公式：x6+x+1

    宽度：6

    Poly值：0x03

    初始值：0x00

    基准输入：true

    基准输出：true

    标志位：0x00

 

 

五、CRC6/MMC：

    公式：x7+x3+1

    宽度：7

    Poly值：0x09

    初始值：0x00

    基准输入：false

    基准输出：false

    标志位：0x00

    使用：MutiMediaCard,SD卡， ect

 

 

六、CRC-8：

    公式：x8+x2+x+1

    宽度：8

    Poly值：0x07

    初始值：0x00

    基准输入：false

    基准输出：false

    标志位：0x00

 

 

七、CRC8/ITU：

    公式：x8+x2+x+1

    宽度：8

    Poly值：0x07

    初始值：0x00

    基准输入：false

    基准输出：false

    标志位：0x55

 

 

八、CRC-8：

    公式：x8+x2+x+1

    宽度：8

    Poly值：0x07

    初始值：0x00

    基准输入：false

    基准输出：false

    标志位：0x00

 

 

九、CRC8/ROHC：

    公式：x8+x2+x+1

    宽度：8

    Poly值：0x07

    初始值：0xFF

    基准输入：true

    基准输出：true

    标志位：0x00

 

 

九、CRC8/ROHC 或 DOW-CRC：

    公式：x8+x5+x4+1

    宽度：8

    Poly值：0x31

    初始值：0x00

    基准输入：true

    基准输出：true

    标志位：0x00
 * CRC-8       x8+x5+x4+1              0x31（0x131）
CRC-8       x8+x2+x1+1              0x07（0x107）
CRC-8       x8+x6+x4+x3+x2+x1       0x5E（0x15E）

2、 CRC校验算法，说白了，就是把需要校验的数据与多项式进行循环异或（XOR），
但进行XOR的方式与实际中数据传输时，是高位先传、还是低位先传有关。对于数据
高位先传的方式，XOR从数据的高位开始，我们就叫它顺序异或吧；对于数据低位先
传的方式，XOR从数据的低位开始，我们就叫它反序异或吧。两种不同的异或方式，
即使对应相同的多项式，计算出来的结果也是不一样的。
下面以顺序异或的例子说明一些计算的过程：
使用多项式：x8+x5+x4+1（二进制为：1 0011 0001）
计算一个字节：0x11（二进制为：00010001）

 *CRC校验是编程中使用比较多的一种检验方式，包括CRC8， CRC16， CRC32校验等。校验长度越长，校验所需要的时间越久。为了缩短计算时间，CRC校验又分为直接计算法和查表计算法。

直接计算是一种按位计算方法，其计算原理如下：

假定有待校验数据A = 1101，校验多项式B = 0000 0111，当前CRC校验值 CRC = 1011 1001, 期待结果C。则计算过程如下：

步骤0：CRC = 1011 1001 首先与数据A左移4位后异或得到新CRC1 =  1011 1001^1101 0000 = 0110 1001;
步骤1：CRC1 首位为0， CRC1左移1位得 CRC2 = 1101 0010;

步骤2：CRC2首位为1， CRC2左移1位得 CRC3 = 1010 0100， 然后CRC2与校验多项式B异或得 CRC4 = 1010 0100 ^ 0000 0111= 1010 0011;

步骤3：CRC4首位为1， CRC4左移1位得CRC5 = 0100 0110， 然后CRC4与校验多项式B异或得 CRC6 = 0100 0110^ 0000 0111 = 0100 0001;

步骤4：CRC6首位为0， CRC6左移1位得CRC7 = 1000 0010;

至此，4位宽度计算完成。

从以上过程可以看出，步骤1到步骤4的执行过程与步骤0所得的结果CRC1的高4位非常相关，该高4位取值直接决定了步骤1到步骤4的运行结果；

进一步将CRC1划分为CRC1 = (0110 0000)^(0000 1001);

则结果就可以写成如下：

C =  (0110 0000)^(0000 1001)^(一系列校验多项式因CRC1高4位取值变化后的多项式)

备注1：步骤0-步骤4中的CRC值左移操作可以认为 是CRC值不变，校验多项式随CRC1高4位取值不同进行变换。

因此，又可以根据CRC1的高4位取值与校验多项式预先生成一张2^4的码表，在实际计算的时候直接取CRC1的低4位(1001)与对应的码值异或，这样来大大加速计算，又称为查表法。依据每次计算位宽度的不同，可以预先计算出2^4,2^8个码值，常用是按照字节计算，则预先准备好2^8个码值。码值的位数则随使用的CRC校验宽度变化。

备注2：实际计算中，校验是从高位向低位，因此，CRC1的低4位(1001)会左移4位，如果在CRC8的校验当中。同理，CRC16校验会左移8位(采用8位宽度校验)，CRC32校验会左移24位(采用8位宽度校验)。

备注3：如果校验中，存在位反转操作，则需要对输入数据进行位反转操作。

备注4： 
 */
//1 0001 0000 0010 0001
static const uint16_t crc16tab[256]= {
    0x0000,0x1021 /*0001 0000 0010 0001*/,0x2042 /*0010 0000 0100 0010*/,0x3063 /*0011 0000 0110 0011*/,0x4084 /*100000010000100*/,0x50a5/*101000010100101*/,0x60c6 /*110000011000110*/,0x70e7/*111000011100111*/,
    0x8108,0x9129,0xa14a,0xb16b,0xc18c,0xd1ad,0xe1ce,0xf1ef,
    0x1231,0x0210,0x3273,0x2252,0x52b5,0x4294,0x72f7,0x62d6,
    0x9339,0x8318,0xb37b,0xa35a,0xd3bd /*1101001110111101*/,0xc39c,0xf3ff,0xe3de,
    0x2462,0x3443,0x0420,0x1401,0x64e6,0x74c7,0x44a4,0x5485,
    0xa56a,0xb54b,0x8528,0x9509,0xe5ee,0xf5cf,0xc5ac,0xd58d,
    0x3653,0x2672,0x1611,0x0630,0x76d7,0x66f6,0x5695,0x46b4,
    0xb75b,0xa77a,0x9719,0x8738,0xf7df,0xe7fe,0xd79d,0xc7bc,
    0x48c4,0x58e5,0x6886,0x78a7,0x0840,0x1861,0x2802,0x3823,
    0xc9cc,0xd9ed,0xe98e,0xf9af,0x8948,0x9969,0xa90a,0xb92b,
    0x5af5,0x4ad4,0x7ab7,0x6a96,0x1a71,0x0a50,0x3a33,0x2a12,
    0xdbfd,0xcbdc,0xfbbf,0xeb9e,0x9b79,0x8b58,0xbb3b,0xab1a,
    0x6ca6,0x7c87,0x4ce4,0x5cc5,0x2c22,0x3c03,0x0c60,0x1c41,
    0xedae,0xfd8f,0xcdec,0xddcd,0xad2a,0xbd0b,0x8d68,0x9d49,
    0x7e97,0x6eb6,0x5ed5,0x4ef4,0x3e13,0x2e32,0x1e51,0x0e70,
    0xff9f,0xefbe,0xdfdd,0xcffc,0xbf1b,0xaf3a,0x9f59,0x8f78,
    0x9188,0x81a9,0xb1ca,0xa1eb,0xd10c,0xc12d,0xf14e,0xe16f,
    0x1080,0x00a1,0x30c2,0x20e3,0x5004,0x4025,0x7046,0x6067,
    0x83b9,0x9398,0xa3fb,0xb3da,0xc33d,0xd31c,0xe37f,0xf35e,
    0x02b1,0x1290,0x22f3,0x32d2,0x4235,0x5214,0x6277,0x7256,
    0xb5ea,0xa5cb,0x95a8,0x8589,0xf56e,0xe54f,0xd52c,0xc50d,
    0x34e2,0x24c3,0x14a0,0x0481,0x7466,0x6447,0x5424,0x4405,
    0xa7db,0xb7fa,0x8799,0x97b8,0xe75f,0xf77e,0xc71d,0xd73c,
    0x26d3,0x36f2,0x0691,0x16b0,0x6657,0x7676,0x4615,0x5634,
    0xd94c,0xc96d,0xf90e,0xe92f,0x99c8,0x89e9,0xb98a,0xa9ab,
    0x5844,0x4865,0x7806,0x6827,0x18c0,0x08e1,0x3882,0x28a3,
    0xcb7d,0xdb5c,0xeb3f,0xfb1e,0x8bf9,0x9bd8,0xabbb,0xbb9a,
    0x4a75,0x5a54,0x6a37,0x7a16,0x0af1,0x1ad0,0x2ab3,0x3a92,
    0xfd2e,0xed0f,0xdd6c,0xcd4d,0xbdaa,0xad8b,0x9de8,0x8dc9,
    0x7c26,0x6c07,0x5c64,0x4c45,0x3ca2,0x2c83,0x1ce0,0x0cc1,
    0xef1f,0xff3e,0xcf5d,0xdf7c,0xaf9b,0xbfba,0x8fd9,0x9ff8,
    0x6e17,0x7e36,0x4e55,0x5e74,0x2e93,0x3eb2,0x0ed1 /*1110 1101 0001*/,0x1ef0 /*1 1110 1111 0000*/
};
/**
 *   0110
    ^1100
     1010
 */
uint16_t crc16(const char *buf, int len) 
{
    int counter;
    uint16_t crc = 0;
    for (counter = 0; counter < len; counter++)
    {
        crc = (crc<<8) ^ crc16tab[((crc>>8) ^ *buf++)&0x00FF];
    }
    return crc;
}



#if test_crc
uint16_t table[256] ={0};
uint16_t byte_crc(uint16_t value)
{
    uint16_t crc = value; 
    for (int i = 16; i > 0; --i)
    {
        if (crc & 0x01)
        {
            crc = (crc >> 1) ^ 0X1021;
        }
        else 
        {
            crc = (crc >> 1);
        }
    }
    return crc;
}
void show_crc16_table()
{

    static int ploy = 0X1021;// 1 0001 0000 0010 0001
    int i = 0;
    for ( i = 0; i < 256; ++i) // 256
    {
        table[i] = i;
    }
    for ( i = 0; i < 256; ++i)
    {
        table[i] = byte_crc(table[i]);
        // table[i] <<= 16; //crc16 算法是以16bit一计算的 
        // table[i] &=ploy;
    }
    for ( i = 0; i < 256; ++i) // 256
    {
        printf("table[%u]=0x%.2x\n", i, table[i]);
    }
}


int main(int argc, char **argv)
{
    char num = 'b' -'a';
    uint16_t crc_num = crc16(&num, 1);
    printf("[ a -b ]crc16 =  %u\n", crc_num);
    show_crc16_table();
    return 0;
}
#endif 