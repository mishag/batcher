#include <batcher.h>
#include <iostream>

struct Record {
    int x;
    int y;
};

class BatchProcessor : public Batcher<Record> {
public:
    BatchProcessor(size_t maxQueueSize,
                   size_t batchSize,
                   size_t waitTime)
        : Batcher<Record>(maxQueueSize, batchSize, waitTime)
    {
    }

private:
    void processBatch(const std::vector<Record>& records)
    {
        std::cout << "Processing " << records.size() << " records\n";
    }
};


int main(int argc, char * argv[])
{
    BatchProcessor batcher(100, 10, 5);
    batcher.start();

    for (int i = 0; i < 100; ++i) {
        Record r;
        batcher.submit(r);
    }

    std::string x;
    std::cin >> x;

    batcher.stop();

    return 0;
}
