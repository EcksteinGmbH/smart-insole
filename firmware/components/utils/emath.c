#include "emath.h"

//type max_buf(type *buf, uint16_t len)
//{
//    type temp = buf[0];
//    for(int i = 0; i < len; i++)
//    {
//        temp = temp > buf[i] ? temp:buf[i];
//    }
//    return temp;
//}


//����ĳ���������½�
uint16_t max_in_buf(uint16_t *ptr, uint16_t size)
{
    uint16_t temp;
    temp = ptr[0];
    for(uint16_t i = 1; i < size; i++)
    {
       if(ptr[i] > temp) temp = ptr[i] ;
    }
    return temp;
}    

uint16_t min_in_buf(uint16_t *ptr, uint16_t size)
{
    uint16_t temp;
    temp = ptr[0];
    for(int i = 0; i < size; i++)
    {
        temp = temp < ptr[i] ? temp:ptr[i];
    }
    return temp;
} 


//uint16_t vpp(uint16_t *ptr, uint16_t size)
//{
//    uint16_t temp_max = max_in_buf(ptr,size);
//    uint16_t temp_min = min_in_buf(ptr,size);
//    return temp_max - temp_min;
//}    



Vpp_t peak_to_peak(uint16_t *ptr, uint16_t size)
{
    Vpp_t info;
    
    info.max = ptr[0];
    info.min = ptr[0];
    for(uint16_t i = 1; i < size; i++)
    {
       if(ptr[i] > info.max) 
       {
           info.max = ptr[i] ;
           info.max_index = i;
       }
       else if(ptr[i] < info.min)
       {
            info.min = ptr[i] ;
           info.min_index = i;
       }
    }   
    info.vpp = info.max - info.min;
    return info;
}    


double avarage(float *ptr, uint16_t size)
{
    double sum = 0;
    for(uint16_t i = 0; i < size; i++)
    {
        sum += ptr[i];
    }
    sum = sum/(float)size;
    return sum;
}
float avarage_u16(uint16_t *ptr, uint16_t size)
{
    float sum = 0;
    for(uint16_t i = 0; i < size; i++)
    {
        sum += ptr[i];
    }
    sum = sum/size;
    return sum;
}

float avarage_float(float *ptr, uint16_t size)
{
    float sum = 0;
    for(uint16_t i = 0; i < size; i++)
    {
        sum += ptr[i];
    }
    sum = sum/size;
    return sum;
}

//���� s= ��(x-u)^2/N;;;
double variance(float *ptr, uint16_t size)
{
    double sum = 0;
    double avg = avarage(ptr,size);
    
    for(uint16_t i = 0; i < size; i++)
    {
        sum += (ptr[i] - avg) * ( ptr[i] - avg);
    }
    return sum/size;
}

//��׼��(������) o = sqrt(var);
double stdev(float *ptr, uint16_t size)
{
    double temp = variance(ptr,size);
    return sqrt(temp);
}

