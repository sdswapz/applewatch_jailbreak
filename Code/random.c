unsigned long long 

GetRandomValue (unsigned long long time, unsigned long long seed)
{
	unsigned int time_low = time & 0xFFFFFFFF;
	unsigned int time_high = (time >> 32) & 0xFFFFFFFF;
	unsigned int result_low;
	unsigned int result_high;
	unsigned int tmp;

	tmp = (time_low & 0xFF) << 8;
	result_low = (time_low ^ tmp) ^ (time_low << 16);

	tmp = (seed & 0xFF) << 16;
	result_high = tmp ^ (result_low ^ time_high);

	tmp = (result_low >> 8) ^ 0XFF;
	result_high = ROTATE_RIGHT(result_high, tmp);

	return (result_high << 32) | result_low;

}