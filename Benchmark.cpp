#include <iostream>
#include <vector>
#include <cmath>
#include <random>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <cstring>
#include <limits>
#include <iomanip>
#include <numeric>

// =========================================================
// 1. ESTRUCTURAS ALINEADAS A LÃNEAS DE CACHÃ‰ (64 BYTES)
// =========================================================
#pragma pack(push, 1)
struct Transaction {
    long long transaction_id;
    double amount;
    long long user_id;
};
#pragma pack(pop)

struct alignas(64) BoundedModel {
    double slope = 0.0;
    double intercept = 0.0;
    long long max_error = 0;

    inline double predict(double x) const { return slope * x + intercept; }
};

class ArenaAllocator {
    size_t chunk_size;
    std::vector<char*> chunks;
    size_t current_chunk_idx, current_offset;

    void allocate_new_chunk() {
        char* new_chunk = static_cast<char*>(malloc(chunk_size));
        if (!new_chunk) throw std::bad_alloc();
        chunks.push_back(new_chunk);
        current_chunk_idx = chunks.size() - 1;
        current_offset = 0;
    }
public:
    ArenaAllocator(size_t chunk_bytes = 256ULL * 1024ULL * 1024ULL) 
        : chunk_size(chunk_bytes), current_chunk_idx(0), current_offset(0) { allocate_new_chunk(); }
    ~ArenaAllocator() { for (char* chunk : chunks) free(chunk); }
    void* allocate(size_t size) {
        size = (size + 7) & ~7; 
        if (current_offset + size > chunk_size) {
            if (size > chunk_size) chunk_size = size * 2;
            allocate_new_chunk();
        }
        void* ptr = chunks[current_chunk_idx] + current_offset;
        current_offset += size;
        return ptr;
    }
};

// =========================================================
// 2. PARSER I/O ULTRA RÃPIDO
// =========================================================
class FastCSVParser {
public:
    static inline long long fast_atoll(const char*& ptr) {
        long long val = 0;
        while (*ptr >= '0' && *ptr <= '9') { val = val * 10 + (*ptr - '0'); ptr++; }
        return val;
    }
    static std::vector<Transaction> load_all_data(const std::string& filename, size_t limit = 13400000) {
        std::vector<Transaction> data;
        std::ifstream file(filename, std::ios::binary); 
        if (!file.is_open()) {
            std::cerr << "[WARNING] Archivo no encontrado. Generando data sintetica para prueba...\n";
            std::mt19937_64 rng(42);
            long long id = 1000;
            for(size_t i=0; i<13305915; ++i) {
                id += static_cast<long long>((rng() % 3) + 1);
                data.push_back({id, static_cast<double>(rng()%1000)/10.0, static_cast<long long>(rng()%100000)});
            }
            return data;
        }
        std::string line;
        std::getline(file, line); 
        data.reserve(limit);
        while (std::getline(file, line)) {
            const char* ptr = line.c_str();
            Transaction tx;
            try {
                tx.transaction_id = fast_atoll(ptr);
                if (tx.transaction_id == 0) continue; 
                if (*ptr == ',') ptr++;
                char* end_ptr; tx.amount = std::strtod(ptr, &end_ptr); ptr = end_ptr;
                if (*ptr == ',') ptr++;
                tx.user_id = fast_atoll(ptr);
                data.push_back(tx);
            } catch (...) { continue; }
        }
        std::sort(data.begin(), data.end(), [](const Transaction& a, const Transaction& b) { return a.transaction_id < b.transaction_id; });
        return data;
    }
};

// =========================================================
// 3. ENTRADA DE ALGORITMOS OPTIMIZADOS
// =========================================================

class SkipList {
public:
    struct Node { long long key; Transaction val; Node** forward; };
private:
    int MAX_LEVEL = 24; float P = 0.5f; int current_level = 1;
    Node* head; ArenaAllocator arena; std::mt19937 rng; std::uniform_real_distribution<float> dist;

    Node* createNode(long long k, Transaction v, int lvl) {
        void* mem = arena.allocate(sizeof(Node) + sizeof(Node*) * static_cast<size_t>(lvl));
        Node* n = new(mem) Node{k, v, reinterpret_cast<Node**>(reinterpret_cast<char*>(mem) + sizeof(Node))};
        for(int i=0; i<lvl; ++i) n->forward[i] = nullptr;
        return n;
    }
public:
    SkipList() : rng(42), dist(0.0f, 1.0f) { head = createNode(0, Transaction{}, MAX_LEVEL); }
    void insert(long long key, Transaction val) {
        std::vector<Node*> update(static_cast<size_t>(MAX_LEVEL), nullptr);
        Node* curr = head;
        for (int i = current_level - 1; i >= 0; i--) {
            while (curr->forward[i] && curr->forward[i]->key < key) curr = curr->forward[i];
            update[static_cast<size_t>(i)] = curr;
        }
        curr = curr->forward[0];
        if (curr && curr->key == key) return;
        int r_lvl = 1; while (dist(rng) < P && r_lvl < MAX_LEVEL) r_lvl++;
        if (r_lvl > current_level) { 
            for (int i = current_level; i < r_lvl; i++) update[static_cast<size_t>(i)] = head; 
            current_level = r_lvl; 
        }
        Node* new_node = createNode(key, val, r_lvl);
        for (int i = 0; i < r_lvl; i++) { 
            new_node->forward[i] = update[static_cast<size_t>(i)]->forward[i]; 
            update[static_cast<size_t>(i)]->forward[i] = new_node; 
        }
    }
    bool search(long long key, Transaction& res) const {
        Node* curr = head;
        for (int i = current_level - 1; i >= 0; i--) {
            while (curr->forward[i] && curr->forward[i]->key < key) curr = curr->forward[i];
        }
        curr = curr->forward[0];
        if (curr && curr->key == key) { res = curr->val; return true; }
        return false;
    }
    Node* get_head() const { return head; }
};

// --- LEARNED SKIP LIST ---
class LearnedSkipList {
    std::vector<SkipList::Node*> index; 
    std::vector<BoundedModel> leaf_models;
    long long min_key, max_key;
    size_t num_leaves = 50000; 
public:
    void train(SkipList* base_sl, size_t total_data) {
        auto* curr = base_sl->get_head()->forward[0];
        index.reserve(total_data);
        while(curr) { index.push_back(curr); curr = curr->forward[0]; }
        if(index.empty()) return;

        min_key = index.front()->key;
        max_key = index.back()->key;
        size_t n = index.size();

        leaf_models.resize(num_leaves);
        size_t segment_size = (n + num_leaves - 1) / num_leaves;

        for (size_t i = 0; i < num_leaves; ++i) {
            size_t start = i * segment_size;
            size_t end = std::min(start + segment_size, n);
            if (start >= end) { leaf_models[i].max_error = 0; continue; }

            double sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;
            double num_elements = static_cast<double>(end - start);
            for (size_t j = start; j < end; ++j) {
                double x = static_cast<double>(index[j]->key); 
                double y = static_cast<double>(j);
                sum_x += x; sum_y += y; sum_xy += x * y; sum_xx += x * x;
            }
            double denom = (num_elements * sum_xx - sum_x * sum_x);
            if (std::abs(denom) < 1e-9) {
                leaf_models[i].slope = 0; leaf_models[i].intercept = static_cast<double>(start);
            } else {
                leaf_models[i].slope = (num_elements * sum_xy - sum_x * sum_y) / denom;
                leaf_models[i].intercept = (sum_y - leaf_models[i].slope * sum_x) / num_elements;
            }

            long long max_err = 0;
            for (size_t j = start; j < end; ++j) {
                long long pred = static_cast<long long>(std::round(leaf_models[i].predict(static_cast<double>(index[j]->key))));
                long long err = std::abs(static_cast<long long>(j) - pred);
                if (err > max_err) max_err = err;
            }
            leaf_models[i].max_error = max_err;
        }
    }

    bool search(long long key, Transaction& res) const {
        if (index.empty() || key < min_key || key > max_key) return false;

        double range = static_cast<double>(max_key - min_key);
        size_t leaf = (range > 0) ? static_cast<size_t>(((static_cast<double>(key) - static_cast<double>(min_key)) / range) * static_cast<double>(num_leaves - 1)) : 0;
        
        const auto& model = leaf_models[leaf];
        long long pos = std::clamp(static_cast<long long>(std::round(model.predict(static_cast<double>(key)))), 0LL, static_cast<long long>(index.size()) - 1);

        long long low = std::max(0LL, pos - model.max_error);
        long long high = std::min(static_cast<long long>(index.size()) - 1, pos + model.max_error);

        if (high - low <= 16) {
            for (long long i = low; i <= high; ++i) {
                if (index[static_cast<size_t>(i)]->key == key) { res = index[static_cast<size_t>(i)]->val; return true; }
            }
        } else {
            auto it = std::lower_bound(index.begin() + low, index.begin() + high + 1, key, [](const auto* node, long long k) {
                return node->key < k;
            });
            if (it != index.end() && (*it)->key == key) { res = (*it)->val; return true; }
        }
        return false;
    }
};

// --- B* TREE TRADICIONAL ---
class BStarTree {
    struct LeafNode { std::vector<Transaction> data; };
    std::vector<LeafNode> leaves;
    std::vector<long long> routing_keys; 
    size_t block_size;
public:
    BStarTree(size_t b_size = 256) : block_size(b_size) {}
    void bulk_load(const std::vector<Transaction>& data) {
        size_t num_leaves = (data.size() + block_size - 1) / block_size;
        leaves.resize(num_leaves);
        routing_keys.reserve(num_leaves);
        for (size_t i = 0; i < num_leaves; ++i) {
            size_t start = i * block_size;
            size_t end = std::min(start + block_size, data.size());
            leaves[i].data.assign(data.begin() + start, data.begin() + end);
            routing_keys.push_back(leaves[i].data.front().transaction_id);
        }
    }
    bool search(long long key, Transaction& res) const {
        if (routing_keys.empty()) return false;
        auto it = std::upper_bound(routing_keys.begin(), routing_keys.end(), key);
        if (it != routing_keys.begin()) --it;
        size_t leaf_idx = static_cast<size_t>(std::distance(routing_keys.begin(), it));
        const auto& leaf = leaves[leaf_idx];
        auto data_it = std::lower_bound(leaf.data.begin(), leaf.data.end(), key, [](const Transaction& tx, long long k) { return tx.transaction_id < k; });
        if (data_it != leaf.data.end() && data_it->transaction_id == key) { res = *data_it; return true; }
        return false;
    }
};

// --- LEARNED B* TREE ---
class LearnedBStarTree {
    std::vector<Transaction> flat_data; 
    std::vector<BoundedModel> leaf_models;
    long long min_key, max_key;
    size_t num_leaves = 50000;
public:
    void train(BStarTree* base_btree, const std::vector<Transaction>& data) {
        (void)base_btree;
        flat_data = data;
        if(flat_data.empty()) return;
        
        min_key = flat_data.front().transaction_id;
        max_key = flat_data.back().transaction_id;
        size_t n = flat_data.size();

        leaf_models.resize(num_leaves);
        size_t segment_size = (n + num_leaves - 1) / num_leaves;

        for (size_t i = 0; i < num_leaves; ++i) {
            size_t start = i * segment_size;
            size_t end = std::min(start + segment_size, n);
            if (start >= end) { leaf_models[i].max_error = 0; continue; }

            double sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;
            double num_elements = static_cast<double>(end - start);
            for (size_t j = start; j < end; ++j) {
                double x = static_cast<double>(flat_data[j].transaction_id); 
                double y = static_cast<double>(j);
                sum_x += x; sum_y += y; sum_xy += x * y; sum_xx += x * x;
            }
            double denom = (num_elements * sum_xx - sum_x * sum_x);
            if (std::abs(denom) < 1e-9) {
                leaf_models[i].slope = 0; leaf_models[i].intercept = static_cast<double>(start);
            } else {
                leaf_models[i].slope = (num_elements * sum_xy - sum_x * sum_y) / denom;
                leaf_models[i].intercept = (sum_y - leaf_models[i].slope * sum_x) / num_elements;
            }

            long long max_err = 0;
            for (size_t j = start; j < end; ++j) {
                long long pred = static_cast<long long>(std::round(leaf_models[i].predict(static_cast<double>(flat_data[j].transaction_id))));
                long long err = std::abs(static_cast<long long>(j) - pred);
                if (err > max_err) max_err = err;
            }
            leaf_models[i].max_error = max_err;
        }
    }

    bool search(long long key, Transaction& res) const {
        if (flat_data.empty() || key < min_key || key > max_key) return false;

        double range = static_cast<double>(max_key - min_key);
        size_t leaf = (range > 0) ? static_cast<size_t>(((static_cast<double>(key) - static_cast<double>(min_key)) / range) * static_cast<double>(num_leaves - 1)) : 0;

        const auto& model = leaf_models[leaf];
        long long pos = std::clamp(static_cast<long long>(std::round(model.predict(static_cast<double>(key)))), 0LL, static_cast<long long>(flat_data.size()) - 1);

        long long low = std::max(0LL, pos - model.max_error);
        long long high = std::min(static_cast<long long>(flat_data.size()) - 1, pos + model.max_error);

        if (high - low <= 16) {
            for (long long i = low; i <= high; ++i) {
                if (flat_data[static_cast<size_t>(i)].transaction_id == key) { res = flat_data[static_cast<size_t>(i)]; return true; }
            }
        } else {
            auto it = std::lower_bound(flat_data.begin() + low, flat_data.begin() + high + 1, key, [](const Transaction& tx, long long k) {
                return tx.transaction_id < k;
            });
            if (it != flat_data.end() && it->transaction_id == key) { res = *it; return true; }
        }
        return false;
    }
};

// =========================================================
// 4. ANÃLISIS ESTADÃSTICO
// =========================================================
struct StatResult { double mean_us, std_dev_us, total_ms; };

StatResult calculate_stats(const std::vector<double>& latencies_us) {
    if(latencies_us.empty()) return {0,0,0};
    double sum = std::accumulate(latencies_us.begin(), latencies_us.end(), 0.0);
    double mean = sum / static_cast<double>(latencies_us.size());
    double sq_sum = 0.0;
    for(double val : latencies_us) sq_sum += (val - mean) * (val - mean);
    return {mean, std::sqrt(sq_sum / static_cast<double>(latencies_us.size())), sum / 1000.0};
}

struct AnovaResult {
    double ssb, ssw, msb, msw, f_value;
    size_t df_b; long long df_w;
};

AnovaResult calculate_one_way_anova(const std::vector<std::vector<double>>& groups) {
    size_t k = groups.size(); long long N = 0; double grand_sum = 0.0;
    std::vector<double> group_means(k, 0.0);
    for (size_t i = 0; i < k; ++i) {
        N += static_cast<long long>(groups[i].size()); 
        double sum = std::accumulate(groups[i].begin(), groups[i].end(), 0.0);
        group_means[i] = sum / static_cast<double>(groups[i].size()); grand_sum += sum;
    }
    double grand_mean = grand_sum / static_cast<double>(N); double ssb = 0.0, ssw = 0.0;
    for (size_t i = 0; i < k; ++i) {
        ssb += static_cast<double>(groups[i].size()) * (group_means[i] - grand_mean) * (group_means[i] - grand_mean);
        for (double val : groups[i]) ssw += (val - group_means[i]) * (val - group_means[i]);
    }
    size_t df_b = k - 1; long long df_w = N - static_cast<long long>(k);
    double msb = ssb / static_cast<double>(df_b); 
    double msw = (df_w > 0) ? (ssw / static_cast<double>(df_w)) : 0;
    return {ssb, ssw, msb, msw, (msw > 0 ? msb / msw : 0), df_b, df_w};
}

// =========================================================
// 5. CONTROLADOR PRINCIPAL
// =========================================================
int main() {
    std::cout << "========================================================\n";
    std::cout << "   SISTEMA EXPERTO DE BENCHMARKING HPC OPTIMIZADO V2   \n";
    std::cout << "========================================================\n";
    
    auto total_data = FastCSVParser::load_all_data("transactions_data.csv");
    size_t DATA_SIZE = total_data.size();
    std::cout << "-> Datos cargados: " << DATA_SIZE << " registros.\n";

    SkipList sl; LearnedSkipList learned_sl;
    BStarTree btree(256); LearnedBStarTree learned_btree;

    std::cout << "\n[Indexando y Entrenando Modelos Bounded-Error al 100%...]\n";
    auto start_train = std::chrono::high_resolution_clock::now();
    for (const auto& tx : total_data) sl.insert(tx.transaction_id, tx);
    btree.bulk_load(total_data);
    learned_sl.train(&sl, DATA_SIZE);
    learned_btree.train(&btree, total_data);
    std::cout << "-> Indexacion completa en: " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_train).count() << " ms\n";

    std::cout << "\nSeleccione tamano de muestra (1 para Todo, 2 para muestra controlada): ";
    int opt; std::cin >> opt;
    size_t sample_size = DATA_SIZE;
    if (opt == 2) { 
        std::cout << "Cantidad de consultas: "; 
        long long input_sample;
        std::cin >> input_sample; 
        sample_size = static_cast<size_t>(input_sample);
    }
    if (sample_size > DATA_SIZE) sample_size = DATA_SIZE;

    std::mt19937 rng(1337); std::vector<long long> queries; queries.reserve(sample_size);
    for(size_t i = 0; i < sample_size; ++i) queries.push_back(total_data[rng() % DATA_SIZE].transaction_id);

    std::cout << "\n--- EJECUTANDO ANALISIS ESTADISTICO DE LATENCIA (" << sample_size << " Consultas) ---\n";
    std::cout << "Estructura               | Tiempo Total | Latencia Media | Desviacion Estandar\n";
    std::cout << "--------------------------------------------------------------------------------\n";
    
    std::vector<std::vector<double>> all_group_latencies; 

    auto run_statistical_test = [&](const std::string& name, const auto& index_struct) {
        std::vector<double> latencies; latencies.reserve(sample_size);
        Transaction dummy;
        for (long long q : queries) {
            auto start = std::chrono::high_resolution_clock::now();
            index_struct.search(q, dummy);
            auto end = std::chrono::high_resolution_clock::now();
            latencies.push_back(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()) / 1000.0);
        }
        StatResult stats = calculate_stats(latencies);
        std::cout << std::left << std::setw(25) << name 
                  << "| " << std::fixed << std::setprecision(2) << std::setw(9) << stats.total_ms << " ms "
                  << "| " << std::setw(11) << stats.mean_us << " us "
                  << "| " << stats.std_dev_us << " us\n";
        all_group_latencies.push_back(std::move(latencies)); 
    };

    run_statistical_test("1. B* Tree Clasico", btree);
    run_statistical_test("2. Learned B* Tree (IA)", learned_btree);
    run_statistical_test("3. Skip List Clasica", sl);
    run_statistical_test("4. Learned Skip List (IA)", learned_sl);
    
    std::cout << "\n================================================================================\n";
    std::cout << "                        TABLA ANOVA (ONE-WAY) DE RENDIMIENTO\n";
    std::cout << "================================================================================\n";
    AnovaResult anova = calculate_one_way_anova(all_group_latencies);
    std::cout << std::left << std::setw(15) << "Origen" << std::setw(20) << "SS" << std::setw(15) << "df" << std::setw(20) << "MS" << "F-Valor\n";
    std::cout << "--------------------------------------------------------------------------------\n";
    std::cout << std::left << std::setw(15) << "Entre Grupos" << std::setw(20) << anova.ssb << std::setw(15) << anova.df_b << std::setw(20) << anova.msb << anova.f_value << "\n";
    std::cout << std::left << std::setw(15) << "Dentro Grupos" << std::setw(20) << anova.ssw << std::setw(15) << anova.df_w << std::setw(20) << anova.msw << "-\n";
    std::cout << "================================================================================\n";
    return 0;
}
