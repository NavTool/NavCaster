#pragma once


// 从第三方拉取数据，推送到本地的频道  


class relay_pull
{
private:
    /* data */
public:
    relay_pull(/* args */);
    ~relay_pull();

    int start(); 
    int stop();
    
private:
    int runing();



};

