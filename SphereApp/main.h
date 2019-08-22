#pragma once
void processMessage(char* message, int length);
int processFunction(unsigned char* name, unsigned char* payload, unsigned char** response, size_t* resp_size);
void setBlueLed(int index);
void versionHandler(unsigned char* version);
unsigned char* getVersion();
void setDistanceflag(bool flag);