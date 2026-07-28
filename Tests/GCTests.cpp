#include "GC/GC.h"

#include <iostream>

namespace
{
	class TestObject final : public GCObject
	{
	public:
		TestObject() { liveCount++; }
		~TestObject() override { liveCount--; }

		GCAllocation* Mark(GCAllocation* marklist) override
		{
			return GC::MarkObject(marklist, child);
		}

		TestObject* child = nullptr;
		static int liveCount;
	};

	int TestObject::liveCount = 0;

	int Fail(const char* message)
	{
		std::cerr << "FAILED: " << message << '\n';
		return 1;
	}
}

int main()
{
	{
		TestObject* parent = GC::Alloc<TestObject>();
		parent->child = GC::Alloc<TestObject>();
		GCRoot<TestObject> parentRoot(parent);

		{
			GCRoot<TestObject> secondRoot(GC::Alloc<TestObject>());
			GC::Collect();
			if (TestObject::liveCount != 3)
				return Fail("collection did not preserve every root and a transitively referenced child");
		}

		GC::Collect();
		if (TestObject::liveCount != 2)
			return Fail("destroying the newest root corrupted the remaining root list");
	}

	GC::Collect();
	if (TestObject::liveCount != 0)
		return Fail("unrooted objects were not collected");

	return 0;
}
