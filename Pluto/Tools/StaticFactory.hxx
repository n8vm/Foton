#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS
#define GLM_FORCE_RIGHT_HANDED
#define SF_VERBOSE 1

#include <glm/glm.hpp>
#include <glm/common.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>

#include <iostream>
#include <string>
#include <set>
#include <map>
#include <vector>
#include <memory>
#include <typeindex>

#include <exception>
#include <mutex>

class StaticFactory {
    public:

    /* items must be printable. */
    virtual std::string to_string() = 0;

    /* returns the name (or key) of the current item. This can be used to look up this components id. */
    virtual std::string get_name() { return name; };

    /* returns the id of the current item. */
    virtual int32_t get_id() { return id; };
    
    /* an item may be empty, in which case 'initialized' will return false. */
    bool is_initialized() {
        return initialized;
    }
    
    /* Returns whether or not a key exists in the lookup table. */
    static bool DoesItemExist(std::map<std::string, uint32_t> &lookupTable, std::string name)
    {
        auto it = lookupTable.find(name);
        return (it != lookupTable.end());
    }

    /* Returns the first index where an item of type T is uninitialized. */
    template<class T>
    static int32_t FindAvailableID(T *items, uint32_t max_items) 
    {
        for (uint32_t i = 0; i < max_items; ++i)
            if (items[i].initialized == false)
                return (int32_t)i;
        return -1;
    }
    
    /* Reserves a location in items and adds an entry in the lookup table */
    template<class T>
    static T* Create(std::shared_ptr<std::mutex> factory_mutex, std::string name, std::string type, std::map<std::string, uint32_t> &lookupTable, T* items, uint32_t maxItems) 
    {
        auto mutex = factory_mutex.get();
        std::lock_guard<std::mutex> lock(*mutex);
        if (DoesItemExist(lookupTable, name))
            throw std::runtime_error(std::string("Error: " + type + " \"" + name + "\" already exists."));

        int32_t id = FindAvailableID(items, maxItems);

        if (id < 0) 
            throw std::runtime_error(std::string("Error: max " + type + " limit reached."));

        // TODO: make this only output if verbose
        #if SF_VERBOSE
        std::cout << "Adding " << type << " \"" << name << "\"" << std::endl;
        #endif
        items[id] = T(name, id);
        lookupTable[name] = id;
        return &items[id];
    }

    /* Retrieves an element with a lookup table indirection */
    template<class T>
    static T* Get(std::shared_ptr<std::mutex> factory_mutex, std::string name, std::string type, std::map<std::string, uint32_t> &lookupTable, T* items, uint32_t maxItems) 
    {
        auto mutex = factory_mutex.get();
        std::lock_guard<std::mutex> lock(*mutex);
        if (DoesItemExist(lookupTable, name)) {
            uint32_t id = lookupTable[name];
            if (!items[id].initialized) return nullptr;
            return &items[id];
        }

        throw std::runtime_error(std::string("Error: " + type + " \"" + name + "\" does not exist."));
        return nullptr;
    }

    /* Retrieves an element by ID directly */
    template<class T>
    static T* Get(std::shared_ptr<std::mutex> factory_mutex, uint32_t id, std::string type, std::map<std::string, uint32_t> &lookupTable, T* items, uint32_t maxItems) 
    {
        auto mutex = factory_mutex.get();
        std::lock_guard<std::mutex> lock(*mutex);
        if (id >= maxItems) 
            throw std::runtime_error(std::string("Error: id greater than max " + type));

        else if (!items[id].initialized) return nullptr;
            //throw std::runtime_error(std::string("Error: " + type + " with id " + std::to_string(id) + " does not exist"));
         
        return &items[id];
    }

    /* Removes an element with a lookup table indirection, removing from both items and the lookup table */
    template<class T>
    static void Delete(std::shared_ptr<std::mutex> factory_mutex, std::string name, std::string type, std::map<std::string, uint32_t> &lookupTable, T* items, uint32_t maxItems)
    {
        auto mutex = factory_mutex.get();
        std::lock_guard<std::mutex> lock(*mutex);
        if (!DoesItemExist(lookupTable, name))
            throw std::runtime_error(std::string("Error: " + type + " \"" + name + "\" does not exist."));

        items[lookupTable[name]] = T();
        lookupTable.erase(name);
    }

    /* If it exists, removes an element with a lookup table indirection, removing from both items and the lookup table */
    template<class T>
    static void DeleteIfExists(std::shared_ptr<std::mutex> factory_mutex, std::string name, std::string type, std::map<std::string, uint32_t> &lookupTable, T* items, uint32_t maxItems)
    {
        auto mutex = factory_mutex.get();
        std::lock_guard<std::mutex> lock(*mutex);
        if (!DoesItemExist(lookupTable, name)) return;
        items[lookupTable[name]] = T();
        lookupTable.erase(name);
    }

    /* Removes an element by ID directly, removing from both items and the lookup table */
    template<class T>
    static void Delete(std::shared_ptr<std::mutex> factory_mutex, uint32_t id, std::string type, std::map<std::string, uint32_t> &lookupTable, T* items, uint32_t maxItems)
    {
        auto mutex = factory_mutex.get();
        std::lock_guard<std::mutex> lock(*mutex);
        if (id >= maxItems)
            throw std::runtime_error(std::string("Error: id greater than max " + type));

        if (!items[id].initialized)
            throw std::runtime_error(std::string("Error: " + type + " with id " + std::to_string(id) + " does not exist"));

        lookupTable.erase(items[id].name);
        items[id] = T();
    }

    protected:

    /* Inheriting factories should set this field to true when a component is considered initialied. */
    bool initialized = false;
    
    /* Inheriting factories should set these fields when a component is created. */
    std::string name = "";
    uint32_t id = -1;

    /* All items keep track of the entities which use them. */
    std::set<uint32_t> entities;
};
#undef SF_VERBOSE