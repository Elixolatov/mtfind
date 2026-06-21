#include <iostream>
#include <fstream>
#include <filesystem>
#include "mtfind_manager.h"


// Executes full search pipeline:
// resets internal state, processes input file in parallel workers, aggregates all matched substrings.
void MtFind::FindMatches(const std::string& filename, const std::string& mask) {
	ValidateInput(filename, mask);
	this->mask = mask;
	// cleanup previous matches
	matches.clear();

	StartWorkers();
	ReadChunks(filename);
	StopWorkers();

	FinalizeResults();
}


// Prints all found matches.
void MtFind::PrintMatches() const {
	std::cout << matches.size() << std::endl;
	for (const auto& match : matches) {
		std::cout
			<< match.line + 1 << " "
			<< match.pos + 1 << " "
			<< match.text << "\n";
	}
}


// Validates filename and mask parameters for search operation.
void MtFind::ValidateInput(const std::string& filename, const std::string& mask) {
	if (!std::filesystem::exists(filename)) {
		throw std::runtime_error("File: '" + filename + "' does not exist.");
	}
	if (mask.empty() || mask.size() > MAX_MASK_SIZE || mask.find('\n') != std::string::npos) {
		throw std::runtime_error("Mask: '" + mask + "' is not correct.");
	}
}


// Reads file into chunks with overlap and pushes it to worker queue.
void MtFind::ReadChunks(const std::string& filename) {
	std::string chunk, overlap;
	const size_t chunk_size = CHUNK_SIZE;
	const size_t overlap_size = mask.size() - 1;

	std::ifstream file(filename, std::ios::binary);
	if (!file) {
		throw std::runtime_error("Failed to open file: " + filename);
	}

	auto read_bytes = [&file](std::string& str, size_t bytes, size_t offset = 0) {
		str.resize(bytes);
		file.read(str.data() + offset, bytes - offset);
		auto readed_bytes = static_cast<size_t>(file.gcount());
		str.resize(offset + readed_bytes);
		return readed_bytes;
		};

	auto save_bytes = [&](size_t chunk_id, std::string&& str) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			chunk_queue.emplace(chunk_id, std::move(str));
		}
		cv.notify_one();
		};

	for (size_t chunk_id = 0;; chunk_id++) {
		if (chunk_id == 0) {
			// first step: chunk is [...], fill full chunk
			if (!read_bytes(chunk, chunk_size)) break;
		}
		else {
			// other steps: chunk is [[overlap][...]], fill empty part
			if (!read_bytes(chunk, chunk_size, overlap_size)) break;
		}

		// last overlap is last chunk, save it as separate chunk
		read_bytes(overlap, overlap_size);
		if (file.peek() == EOF && !overlap.empty()) {
			save_bytes(chunk_id + 1, std::string(overlap));
		}

		save_bytes(chunk_id, chunk + overlap);
		std::swap(chunk, overlap);
	}
}


// Starts worker threads for parallel chunk processing.
void MtFind::StartWorkers() {
	size_t workers_count = std::max(2u, std::thread::hardware_concurrency());
	workers.reserve(workers_count);
	for (size_t i = 0; i < workers_count; i++) {
		workers.emplace_back([this] { WorkerLoop(); });
	}
}


// Stops worker threads and joins thread pool.
void MtFind::StopWorkers() {
	{
		std::lock_guard<std::mutex> lock(mutex);
		stop = true;
	}
	cv.notify_all();
	for (size_t i = 0; i < workers.size(); i++) {
		if (workers[i].joinable()) {
			workers[i].join();
		}
	}
	// cleanup workers state
	workers.clear();
	stop = false;
}


// Worker thread main loop: get and process chunks from queue.
void MtFind::WorkerLoop() {
	while (true) {
		std::unique_lock<std::mutex> lock(mutex);
		cv.wait(lock, [&]() { return !chunk_queue.empty() || stop; });

		if (stop && chunk_queue.empty()) {
			break;
		}

		Chunk chunk = std::move(chunk_queue.front());
		chunk_queue.pop();
		lock.unlock();

		ChunkResult chunk_result = ProcessChunk(chunk);

		lock.lock();
		chunk_results.push_back(chunk_result);
	}
}


// Processes a chunk and collects all matching substrings.
MtFind::ChunkResult MtFind::ProcessChunk(const Chunk& chunk) {
	ChunkResult result = ChunkResult(chunk.id, 0, 0, {});
	if (chunk.data.size() < mask.size()) {
		return result;
	}

	// calculate chunk stats exlude overlap, last chunk can be smaler then general chunk
	size_t chunk_size = chunk.data.size() > CHUNK_SIZE ? CHUNK_SIZE : chunk.data.size();
	for (size_t i = 0; i < chunk_size; i++) {
		if (chunk.data[i] == '\n') {
			result.line_count++;
			result.last_line_length = 0;
			continue;
		}
		result.last_line_length++;
	}

	// find all matches in file
	size_t match_line = 0, match_pos = 0;
	for (size_t i = 0; i + mask.size() <= chunk.data.size(); i++) {
		if (chunk.data[i] == '\n') {
			match_line++;
			match_pos = 0;
			continue;
		}

		bool match = true;
		for (size_t j = 0; j < mask.size(); j++) {
			if (chunk.data[i + j] == '\n') {
				match = false;
				break;
			}
			if (mask[j] != '?' && mask[j] != chunk.data[i + j]) {
				match = false;
				break;
			}
		}

		if (match) {
			result.matches.emplace_back(match_line, match_pos, chunk.data.substr(i, mask.size()));
			match_pos += mask.size() - 1;
			i += mask.size() - 1;
		}
		match_pos++;
	}

	return result;
}


// Finalize chunk results into matches list.
void MtFind::FinalizeResults() {
	size_t total_matches = 0;
	size_t cur_line = 0;
	size_t cur_line_length = 0;

	// sort chunk_results by chunk id
	std::sort(chunk_results.begin(), chunk_results.end(),
		[](const auto& lhs, const auto& rhs) {
			return lhs.id < rhs.id;
		});

	// calculates match position in file from position within chunk and removes intersecting matches
	Match* last_match = nullptr;
	for (auto& chunk : chunk_results) {
		for (auto it = chunk.matches.begin(); it != chunk.matches.end(); ) {
			if (it->line == 0) {
				it->pos += cur_line_length;
			}
			it->line += cur_line;

			bool intersects = last_match && last_match->line == it->line && (last_match->pos + mask.size()) > it->pos;
			if (intersects) {
				it = chunk.matches.erase(it);
				continue;
			}

			last_match = &(*it);
			it++;
		}

		total_matches += chunk.matches.size();
		cur_line += chunk.line_count;
		// update last line length across chunks
		cur_line_length = chunk.line_count ? chunk.last_line_length : cur_line_length + chunk.last_line_length;
	}

	// puts chunk.matches to global matches.
	matches.reserve(total_matches);
	for (auto& chunk : chunk_results) {
		matches.insert(matches.end(),
			std::make_move_iterator(chunk.matches.begin()),
			std::make_move_iterator(chunk.matches.end()));
	}
	chunk_results.clear();
}
