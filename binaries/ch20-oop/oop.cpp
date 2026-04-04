/*
 * oop.cpp — Application C++ orientée objet avec système de plugins
 *
 * Formation Reverse Engineering — Chapitre 20
 * Licence MIT — Usage strictement éducatif
 *
 * Démontre :
 *   - Héritage avec méthodes virtuelles (vtable / vptr)
 *   - RTTI et dynamic_cast
 *   - Chargement dynamique de plugins (dlopen/dlsym)
 *   - Utilisation de conteneurs STL (std::vector, std::string, std::map)
 *   - Smart pointers (std::unique_ptr)
 *   - Exceptions C++
 *
 * Conçu pour être analysé dans Ghidra : reconstruire la hiérarchie de
 * classes, les vtables et les structures de données.
 */

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>

/* ========================================================================
 * Hiérarchie de classes : Device -> Sensor / Actuator
 * ======================================================================== */

class Device {
public:
    Device(const std::string &name, uint32_t id)
        : name_(name), id_(id), active_(false) {}

    virtual ~Device() = default;

    virtual std::string type_name() const = 0;
    virtual void        initialize()       = 0;
    virtual void        process()          = 0;
    virtual std::string status_report() const;

    const std::string &name() const { return name_; }
    uint32_t           id()   const { return id_; }
    bool               is_active() const { return active_; }

protected:
    void set_active(bool state) { active_ = state; }

private:
    std::string name_;
    uint32_t    id_;
    bool        active_;
};

std::string Device::status_report() const {
    return "[" + type_name() + "] " + name_ +
           " (id=" + std::to_string(id_) + ", " +
           (active_ ? "ACTIVE" : "INACTIVE") + ")";
}

/* --- Sensor : hérite de Device --- */

class Sensor : public Device {
public:
    Sensor(const std::string &name, uint32_t id,
           double min_range, double max_range)
        : Device(name, id),
          min_range_(min_range), max_range_(max_range),
          last_value_(0.0), read_count_(0) {}

    std::string type_name() const override { return "Sensor"; }

    void initialize() override {
        last_value_ = 0.0;
        read_count_ = 0;
        set_active(true);
        std::cout << "  [Sensor] " << name() << " initialise ("
                  << min_range_ << " - " << max_range_ << ")\n";
    }

    void process() override {
        if (!is_active())
            throw std::runtime_error("Sensor " + name() + " non initialise");

        /* Simulation d'une lecture capteur */
        double range = max_range_ - min_range_;
        last_value_ = min_range_ +
            (range * ((double)(read_count_ * 17 + 42) / 256.0));
        if (last_value_ > max_range_)
            last_value_ = min_range_ + (last_value_ - max_range_);

        read_count_++;
    }

    double last_value() const { return last_value_; }

    std::string status_report() const override {
        return Device::status_report() +
               " val=" + std::to_string(last_value_) +
               " reads=" + std::to_string(read_count_);
    }

private:
    double   min_range_;
    double   max_range_;
    double   last_value_;
    uint32_t read_count_;
};

/* --- Actuator : hérite de Device --- */

class Actuator : public Device {
public:
    enum class State : uint8_t {
        OFF     = 0,
        ON      = 1,
        ERROR   = 2
    };

    Actuator(const std::string &name, uint32_t id, uint32_t max_power)
        : Device(name, id),
          state_(State::OFF), power_level_(0), max_power_(max_power) {}

    std::string type_name() const override { return "Actuator"; }

    void initialize() override {
        state_       = State::OFF;
        power_level_ = 0;
        set_active(true);
        std::cout << "  [Actuator] " << name() << " initialise (max "
                  << max_power_ << "W)\n";
    }

    void process() override {
        if (!is_active())
            throw std::runtime_error("Actuator " + name() + " non initialise");

        if (state_ == State::ON && power_level_ > max_power_) {
            state_ = State::ERROR;
            std::cerr << "  [!] " << name() << " surcharge !\n";
        }
    }

    void set_power(uint32_t level) {
        power_level_ = level;
        state_ = (level > 0) ? State::ON : State::OFF;
    }

    std::string status_report() const override {
        const char *state_str[] = {"OFF", "ON", "ERROR"};
        return Device::status_report() +
               " state=" + state_str[static_cast<uint8_t>(state_)] +
               " power=" + std::to_string(power_level_) + "W";
    }

private:
    State    state_;
    uint32_t power_level_;
    uint32_t max_power_;
};

/* ========================================================================
 * DeviceManager — gestion d'une flotte de devices
 * ======================================================================== */

class DeviceManager {
public:
    DeviceManager() : next_id_(1) {}

    uint32_t add_sensor(const std::string &name,
                        double min_r, double max_r) {
        uint32_t id = next_id_++;
        devices_.push_back(std::make_unique<Sensor>(name, id, min_r, max_r));
        name_index_[name] = devices_.size() - 1;
        return id;
    }

    uint32_t add_actuator(const std::string &name, uint32_t max_power) {
        uint32_t id = next_id_++;
        devices_.push_back(std::make_unique<Actuator>(name, id, max_power));
        name_index_[name] = devices_.size() - 1;
        return id;
    }

    void initialize_all() {
        std::cout << "[DeviceManager] Initialisation de "
                  << devices_.size() << " devices...\n";
        for (auto &dev : devices_) {
            dev->initialize();
        }
    }

    void process_all() {
        for (auto &dev : devices_) {
            try {
                dev->process();
            } catch (const std::runtime_error &e) {
                std::cerr << "[ERR] " << e.what() << "\n";
            }
        }
    }

    void print_report() const {
        std::cout << "\n=== Rapport des Devices ===\n";
        for (const auto &dev : devices_) {
            std::cout << "  " << dev->status_report() << "\n";
        }
        std::cout << "=== Fin du rapport ===\n\n";
    }

    Device *find_by_name(const std::string &name) {
        auto it = name_index_.find(name);
        if (it == name_index_.end())
            return nullptr;
        return devices_[it->second].get();
    }

    size_t device_count() const { return devices_.size(); }

private:
    std::vector<std::unique_ptr<Device>> devices_;
    std::map<std::string, size_t>        name_index_;
    uint32_t                             next_id_;
};

/* ========================================================================
 * Interface Plugin — chargement dynamique (.so via dlopen)
 * ======================================================================== */

/* Convention : un plugin .so exporte ces deux fonctions C */
typedef const char *(*plugin_name_fn)(void);
typedef void        (*plugin_run_fn)(DeviceManager *mgr);

struct PluginHandle {
    void          *dl_handle;
    std::string    name;
    plugin_run_fn  run;
};

static bool load_plugin(const char *path, PluginHandle &out) {
    void *handle = dlopen(path, RTLD_LAZY);
    if (!handle) {
        std::cerr << "[Plugin] Erreur dlopen: " << dlerror() << "\n";
        return false;
    }

    auto name_fn = (plugin_name_fn)dlsym(handle, "plugin_name");
    auto run_fn  = (plugin_run_fn)dlsym(handle, "plugin_run");

    if (!name_fn || !run_fn) {
        std::cerr << "[Plugin] Symboles manquants dans " << path << "\n";
        dlclose(handle);
        return false;
    }

    out.dl_handle = handle;
    out.name      = name_fn();
    out.run       = run_fn;
    return true;
}

static void unload_plugin(PluginHandle &p) {
    if (p.dl_handle) {
        dlclose(p.dl_handle);
        p.dl_handle = nullptr;
    }
}

/* ========================================================================
 * Point d'entree
 * ======================================================================== */

int main(int argc, char *argv[]) {
    std::cout << "+-----------------------------------------+\n"
              << "|   OOP Device Manager -- RE Training      |\n"
              << "+-----------------------------------------+\n\n";

    DeviceManager mgr;

    /* Creer quelques devices */
    mgr.add_sensor("Thermometre_A",  -40.0, 125.0);
    mgr.add_sensor("Pression_B",       0.0, 1013.25);
    mgr.add_sensor("Humidite_C",       0.0, 100.0);
    mgr.add_actuator("Moteur_X",   500);
    mgr.add_actuator("Vanne_Y",    100);

    mgr.initialize_all();
    mgr.print_report();

    /* Simuler quelques cycles */
    for (int cycle = 0; cycle < 3; cycle++) {
        std::cout << "--- Cycle " << cycle + 1 << " ---\n";

        /* Piloter un actuator via dynamic_cast */
        Device *dev = mgr.find_by_name("Moteur_X");
        if (dev) {
            Actuator *act = dynamic_cast<Actuator *>(dev);
            if (act) {
                act->set_power((cycle + 1) * 200);
            }
        }

        mgr.process_all();
        mgr.print_report();
    }

    /* Chargement optionnel de plugins passes en argument */
    std::vector<PluginHandle> plugins;
    for (int i = 1; i < argc; i++) {
        PluginHandle ph{};
        if (load_plugin(argv[i], ph)) {
            std::cout << "[Plugin] Charge : " << ph.name << "\n";
            ph.run(&mgr);
            plugins.push_back(std::move(ph));
        }
    }

    /* Nettoyage */
    for (auto &p : plugins)
        unload_plugin(p);

    std::cout << "[+] Termine. " << mgr.device_count() << " devices geres.\n";
    return 0;
}
