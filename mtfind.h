#ifndef MTFIND_H
#define MTFIND_H

#include <condition_variable>
#include <thread>
#include <mutex>
#include <vector>
#include <queue>


struct Match {
	size_t line;
	size_t pos;
	std::string text;
};


class MtFind {
private:
	struct Chunk {
		size_t id;
		std::string data;
	};

	struct ChunkResult {
		size_t id;
		size_t line_count;
		size_t last_line_length;
		std::vector<Match> matches;
	};

public:
	void FindMatches(const std::string& filename, const std::string& mask);
	void PrintMatches() const;
	const std::vector<Match>& Matches() const { return matches; };

private:
	void ValidateInput(const std::string& filename, const std::string& mask);
	void ReadChunks(const std::string& filename);

	void StartWorkers();
	void StopWorkers();
	void WorkerLoop();

	ChunkResult ProcessChunk(const Chunk& chunk);
	void FinalizeResults();

private:
	static constexpr size_t CHUNK_SIZE = 256 * 1024;
	static const size_t MAX_MASK_SIZE = 100000;

	bool stop = false;
	std::mutex mutex;
	std::condition_variable cv;
	std::vector<std::thread> workers;

	std::string mask;

	std::queue<Chunk> chunk_queue;
	std::vector<ChunkResult> chunk_results;
	std::vector<Match> matches;
};


#endif MTFIND_H