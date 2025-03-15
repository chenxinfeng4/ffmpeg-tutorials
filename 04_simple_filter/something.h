#include <iostream>
#include <sstream>
#include <cstdlib> // for exit()
#include <cassert>

void CHECK(bool condition){
    assert(condition);
}
void CHECK_NOTNULL(const void* ptr){
    assert(ptr!= nullptr);
}