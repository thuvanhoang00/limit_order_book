#include <iostream>
#include <mutex>
#include <thread>
#include <chrono>
#include <random>

#define N 10
int buffer[N];
int count=0;

int producer_idx=0;
int consumer_idx=0;

std::mutex mtx;

std::random_device rd_seed;
std::mt19937 algo(rd_seed());
std::uniform_int_distribution rd_dist_range(100,1000);


void produce(){
    while (true)
    {
        std::unique_lock lock(mtx);
        if (count == N)
        {
            std::cout << "Buffer is full\n";
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(rd_dist_range(algo)));
        }
        auto before = buffer[producer_idx];
        buffer[producer_idx] = producer_idx;
        std::cout << "Produce " << producer_idx << ": "
                  << "before: " << before << "-after: " << buffer[producer_idx] << std::endl;
        producer_idx = (producer_idx + 1) % N;
        count++;
        std::this_thread::sleep_for(std::chrono::milliseconds(rd_dist_range(algo)));
    }
}

void consume(){
    while(true)
    {
        std::unique_lock lock(mtx);
        if (count == 0)
        {
            std::cout << "Buffer is empty\n";
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(rd_dist_range(algo)));
        }
        std::cout << "Consume " << consumer_idx << ": " << buffer[consumer_idx] << std::endl;
        buffer[consumer_idx]=0;
        count--;
        consumer_idx = (consumer_idx + 1) % N;
        std::this_thread::sleep_for(std::chrono::milliseconds(rd_dist_range(algo)));
    }
}

int main()
{
    std::thread t1(produce);
    std::thread t2(consume);
    t1.join();
    t2.join();
    return 0;
}