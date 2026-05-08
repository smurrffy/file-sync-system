#include<stdio.h>

main()
{ 
   char s[] = "Hello\0Hi";
   
   printf("%d %d", strlen(s), sizeof(s));
}

