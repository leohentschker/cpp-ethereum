/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file vm.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 * vm test functions.
 */

#include "vm.h"
#include <test/tools/libtestutils/TestLastBlockHashes.h>
#include <libethereum/ChainParams.h>
#include <libethereum/Executive.h>
#include <libevm/VMFactory.h>
#include <libevm/ExtVMFace.h>
#include <boost/filesystem.hpp>


using namespace std;
using namespace json_spirit;
using namespace dev;
using namespace dev::eth;
using namespace dev::test;

FakeExtVM::FakeExtVM(EnvInfo const& _envInfo, unsigned _depth):			/// TODO: XXX: remove the default argument & fix.
	ExtVMFace(_envInfo, Address(), Address(), Address(), 0, 1, bytesConstRef(), bytes(), EmptySHA3, false, _depth)
{}

std::pair<h160, eth::owning_bytes_ref> FakeExtVM::create(u256 _endowment, u256& io_gas, bytesConstRef _init, Instruction , u256, OnOpFunc const&)
{
	Address na = right160(sha3(rlpList(myAddress, get<1>(addresses[myAddress]))));
	Transaction t(_endowment, gasPrice, io_gas, _init.toBytes());
	callcreates.push_back(t);
	return {na, eth::owning_bytes_ref{}};
}

std::pair<bool, eth::owning_bytes_ref> FakeExtVM::call(CallParameters& _p)
{
	Transaction t(_p.valueTransfer, gasPrice, _p.gas, _p.receiveAddress, _p.data.toVector());
	callcreates.push_back(t);
	return {true, eth::owning_bytes_ref{}};  // Return empty output.
}

h256 FakeExtVM::blockHash(u256 _number)
{
	cnote << "Warning: using fake blockhash code!\n";
	if (_number < envInfo().number() && _number >= (std::max<u256>(256, envInfo().number()) - 256))
		return sha3(toString(_number));

	return h256();
}

void FakeExtVM::set(Address _a, u256 _myBalance, u256 _myNonce, map<u256, u256> const& _storage, bytes const& _code)
{
	get<0>(addresses[_a]) = _myBalance;
	get<1>(addresses[_a]) = _myNonce;
	get<2>(addresses[_a]) = _storage;
	get<3>(addresses[_a]) = _code;
}

void FakeExtVM::reset(u256 _myBalance, u256 _myNonce, map<u256, u256> const& _storage)
{
	callcreates.clear();
	addresses.clear();
	set(myAddress, _myBalance, _myNonce, _storage, get<3>(addresses[myAddress]));
}

mObject FakeExtVM::exportEnv()
{
	mObject ret;
	ret["currentDifficulty"] = toCompactHexPrefixed(envInfo().difficulty(), 1);
	ret["currentTimestamp"] =  toCompactHexPrefixed(envInfo().timestamp(), 1);
	ret["currentCoinbase"] = toHexPrefixed(envInfo().author());
	ret["currentNumber"] = toCompactHexPrefixed(envInfo().number(), 1);
	ret["currentGasLimit"] = toCompactHexPrefixed(envInfo().gasLimit(), 1);
	return ret;
}

EnvInfo FakeExtVM::importEnv(mObject& _o, LastBlockHashesFace const& _lastBlockHashes)
{
	// cant use BOOST_REQUIRE, because this function is used outside boost test (createRandomTest)
	assert(_o.count("currentGasLimit") > 0);
	assert(_o.count("currentDifficulty") > 0);
	assert(_o.count("currentTimestamp") > 0);
	assert(_o.count("currentCoinbase") > 0);
	assert(_o.count("currentNumber") > 0);
	auto gasLimit = toInt(_o["currentGasLimit"]);
	assert(gasLimit <= std::numeric_limits<int64_t>::max());

	BlockHeader blockHeader;
	blockHeader.setGasLimit(gasLimit.convert_to<int64_t>());
	blockHeader.setDifficulty(toInt(_o["currentDifficulty"]));
	blockHeader.setTimestamp(toInt(_o["currentTimestamp"]));
	blockHeader.setAuthor(Address(_o["currentCoinbase"].get_str()));
	blockHeader.setNumber(toInt(_o["currentNumber"]));
	return EnvInfo(blockHeader, _lastBlockHashes, 0);
}

mObject FakeExtVM::exportState()
{
	mObject ret;
	for (auto const& a: addresses)
	{
		mObject o;
		o["balance"] = toCompactHexPrefixed(get<0>(a.second), 1);
		o["nonce"] = toCompactHexPrefixed(get<1>(a.second), 1);
		{
			mObject store;
			for (auto const& s: get<2>(a.second))
				store[toCompactHexPrefixed(s.first, 1)] = toCompactHexPrefixed(s.second, 1);
			o["storage"] = store;
		}
		o["code"] = toHexPrefixed(get<3>(a.second));
		ret[toHexPrefixed(a.first)] = o;
	}
	return ret;
}

void FakeExtVM::importState(mObject& _object)
{
	for (auto const& i: _object)
	{
		mObject o = i.second.get_obj();
		// cant use BOOST_REQUIRE, because this function is used outside boost test (createRandomTest)
		assert(o.count("balance") > 0);
		assert(o.count("nonce") > 0);
		assert(o.count("storage") > 0);
		assert(o.count("code") > 0);

		auto& a = addresses[Address(i.first)];
		get<0>(a) = toInt(o["balance"]);
		get<1>(a) = toInt(o["nonce"]);
		for (auto const& j: o["storage"].get_obj())
			get<2>(a)[toInt(j.first)] = toInt(j.second);

		get<3>(a) = importCode(o);
	}
}

mObject FakeExtVM::exportExec()
{
	mObject ret;
	ret["address"] = toHexPrefixed(myAddress);
	ret["caller"] = toHexPrefixed(caller);
	ret["origin"] = toHexPrefixed(origin);
	ret["value"] = toCompactHexPrefixed(value, 1);
	ret["gasPrice"] = toCompactHexPrefixed(gasPrice, 1);
	ret["gas"] = toCompactHexPrefixed(execGas, 1);
	ret["data"] = toHexPrefixed(data);
	ret["code"] = toHexPrefixed(code);
	return ret;
}

void FakeExtVM::importExec(mObject& _o)
{
	// cant use BOOST_REQUIRE, because this function is used outside boost test (createRandomTest)
	assert(_o.count("address")> 0);
	assert(_o.count("caller") > 0);
	assert(_o.count("origin") > 0);
	assert(_o.count("value") > 0);
	assert(_o.count("data") > 0);
	assert(_o.count("gasPrice") > 0);
	assert(_o.count("gas") > 0);

	myAddress = Address(_o["address"].get_str());
	caller = Address(_o["caller"].get_str());
	origin = Address(_o["origin"].get_str());
	value = toInt(_o["value"]);
	gasPrice = toInt(_o["gasPrice"]);
	gas = toInt(_o["gas"]);
	execGas = gas;

	thisTxCode.clear();
	code = thisTxCode;

	thisTxCode = importCode(_o);
	if (_o["code"].type() != str_type && _o["code"].type() != array_type)
		code.clear();

	thisTxData.clear();
	thisTxData = importData(_o);

	data = &thisTxData;
}

mArray FakeExtVM::exportCallCreates()
{
	mArray ret;
	for (Transaction const& tx: callcreates)
	{
		mObject o;
		o["destination"] = tx.isCreation() ? "" : toHexPrefixed(tx.receiveAddress());
		o["gasLimit"] = toCompactHexPrefixed(tx.gas(), 1);
		o["value"] = toCompactHexPrefixed(tx.value(), 1);
		o["data"] = toHexPrefixed(tx.data());
		ret.push_back(o);
	}
	return ret;
}

void FakeExtVM::importCallCreates(mArray& _callcreates)
{
	for (mValue& v: _callcreates)
	{
		auto tx = v.get_obj();
		assert(tx.count("data") > 0);
		assert(tx.count("value") > 0);
		assert(tx.count("destination") > 0);
		assert(tx.count("gasLimit") > 0);
		Transaction t = tx["destination"].get_str().empty() ?
			Transaction(toInt(tx["value"]), 0, toInt(tx["gasLimit"]), fromHex(tx["data"].get_str())) :
			Transaction(toInt(tx["value"]), 0, toInt(tx["gasLimit"]), Address(tx["destination"].get_str()), fromHex(tx["data"].get_str()));
		callcreates.push_back(t);
	}
}

eth::OnOpFunc FakeExtVM::simpleTrace() const
{

	return [](uint64_t steps, uint64_t pc, eth::Instruction inst, bigint newMemSize, bigint gasCost, bigint gas, dev::eth::VM* voidVM, dev::eth::ExtVMFace const* voidExt)
	{
		FakeExtVM const& ext = *static_cast<FakeExtVM const*>(voidExt);
		eth::VM& vm = *voidVM;

		std::ostringstream o;
		o << std::endl << "    STACK" << std::endl;
		for (auto i: vm.stack())
			o << (h256)i << std::endl;
		o << "    MEMORY" << std::endl << memDump(vm.memory());
		o << "    STORAGE" << std::endl;

		for (auto const& i: std::get<2>(ext.addresses.find(ext.myAddress)->second))
			o << std::showbase << std::hex << i.first << ": " << i.second << std::endl;

		dev::LogOutputStream<eth::VMTraceChannel, false>() << o.str();
		dev::LogOutputStream<eth::VMTraceChannel, false>() << " | " << std::dec << ext.depth << " | " << ext.myAddress << " | #" << steps << " | " << std::hex << std::setw(4) << std::setfill('0') << pc << " : " << instructionInfo(inst).name << " | " << std::dec << gas << " | -" << std::dec << gasCost << " | " << newMemSize << "x32" << " ]";

		/*creates json stack trace*/
		if (eth::VMTraceChannel::verbosity <= g_logVerbosity)
		{
			Object o_step;

			/*add the stack*/
			Array a_stack;
			for (auto i: vm.stack())
				a_stack.push_back((string)i);

			o_step.push_back(Pair( "stack", a_stack ));

			/*add the memory*/
			Array a_mem;
			for(auto i: vm.memory())
				a_mem.push_back(i);

			o_step.push_back(Pair("memory", a_mem));

			/*add the storage*/
			Object storage;
			for (auto const& i: std::get<2>(ext.addresses.find(ext.myAddress)->second))
				storage.push_back(Pair( (string)i.first , (string)i.second));

			/*add all the other details*/
			o_step.push_back(Pair("storage", storage));
			o_step.push_back(Pair("depth", to_string(ext.depth)));
			o_step.push_back(Pair("gas", (string)gas));
			o_step.push_back(Pair("address", toString(ext.myAddress )));
			o_step.push_back(Pair("step", steps ));
			o_step.push_back(Pair("pc", pc));
			o_step.push_back(Pair("opcode", instructionInfo(inst).name ));

			/*append the JSON object to the log file*/
			Value v(o_step);
			ofstream os( "./stackTrace.json", ofstream::app);
			os << write_string(v, true) << ",";
			os.close();
		}
	};
}

namespace dev { namespace test {

json_spirit::mValue doVMTests(json_spirit::mValue const& _input, bool _fillin)
{
	json_spirit::mValue v = _input; // TODO: avoid copying and only add valid fields into the new object.
	if (string(boost::unit_test::framework::current_test_case().p_name) != "vmRandom")
		TestOutputHelper::initTest(v);

	for (auto& i: v.get_obj())
	{
		string testname = i.first;
		json_spirit::mObject& o = i.second.get_obj();

		if (!TestOutputHelper::passTest(testname))
		{
			o.clear(); //don't add irrelevant tests to the final file when filling
			continue;
		}

		BOOST_REQUIRE_MESSAGE(o.count("env") > 0, testname + " env not set!");
		BOOST_REQUIRE_MESSAGE(o.count("pre") > 0, testname + " pre not set!");
		BOOST_REQUIRE_MESSAGE(o.count("exec") > 0, testname + " exec not set!");
		if (! _fillin)
			BOOST_REQUIRE_MESSAGE(o.count("expect") == 0, testname + " expect set!");

		TestLastBlockHashes lastBlockHashes(h256s(256, h256()));
		eth::EnvInfo env = FakeExtVM::importEnv(o["env"].get_obj(), lastBlockHashes);
		FakeExtVM fev(env);
		fev.importState(o["pre"].get_obj());

		if (_fillin)
			o["pre"] = mValue(fev.exportState());

		fev.importExec(o["exec"].get_obj());
		if (fev.code.empty())
		{
			fev.thisTxCode = get<3>(fev.addresses.at(fev.myAddress));
			fev.code = fev.thisTxCode;
		}
		fev.codeHash = sha3(fev.code);

		owning_bytes_ref output;
		bool vmExceptionOccured = false;
		try
		{
			auto vm = eth::VMFactory::create();
			auto vmtrace = Options::get().vmtrace ? fev.simpleTrace() : OnOpFunc{};
			{
				Listener::ExecTimeGuard guard{i.first};
				auto gas = static_cast<int64_t>(fev.gas);
				output = vm->exec(fev.gas, fev, vmtrace);
				gas -= static_cast<int64_t>(fev.gas);
				guard.setGasUsed(gas);
			}
		}
		catch (VMException const&)
		{
			cnote << "    Safe VM Exception\n";
			vmExceptionOccured = true;
		}
		catch (Exception const& _e)
		{
			cnote << "VM did throw an exception: " << diagnostic_information(_e);
			BOOST_ERROR("Failed VM Test with Exception: " << _e.what());
		}
		catch (std::exception const& _e)
		{
			cnote << "VM did throw an exception: " << _e.what();
			BOOST_ERROR("Failed VM Test with Exception: " << _e.what());
		}

		// delete null entries in storage for the sake of comparison

		for (auto  &a: fev.addresses)
		{
			vector<u256> keystoDelete;
			for (auto &s: get<2>(a.second))
			{
				if (s.second == 0)
					keystoDelete.push_back(s.first);
			}
			for (auto const key: keystoDelete )
			{
				get<2>(a.second).erase(key);
			}
		}

		if (_fillin)
		{
			o["env"] = mValue(fev.exportEnv());
			o["exec"] = mValue(fev.exportExec());
			if (vmExceptionOccured)
			{
				if (o.count("expect") > 0)
				{
					BOOST_REQUIRE_MESSAGE(o.count("expect") == 1, testname + " multiple expect set!");
					State postState(State::Null);
					State expectState(State::Null);
					AccountMaskMap expectStateMap;
					ImportTest::importState(mValue(fev.exportState()).get_obj(), postState);
					ImportTest::importState(o["expect"].get_obj(), expectState, expectStateMap);
					ImportTest::compareStates(expectState, postState, expectStateMap, WhenError::Throw);
					o.erase(o.find("expect"));
				}
				BOOST_REQUIRE_MESSAGE(o.count("expect") == 0, testname + " expect should have been erased!");
			}
			else
			{
				o["post"] = mValue(fev.exportState());

				if (o.count("expect") > 0)
				{
					BOOST_REQUIRE_MESSAGE(o.count("expect") == 1, testname + " multiple expect set!");

					State postState(State::Null);
					State expectState(State::Null);
					AccountMaskMap expectStateMap;
					ImportTest::importState(o["post"].get_obj(), postState);
					ImportTest::importState(o["expect"].get_obj(), expectState, expectStateMap);
					ImportTest::compareStates(expectState, postState, expectStateMap, WhenError::Throw);
					o.erase(o.find("expect"));
				}

				BOOST_REQUIRE_MESSAGE(o.count("expect") == 0, testname + " expect should have been erased!");

				o["callcreates"] = fev.exportCallCreates();
				o["out"] = output.size() > 4096 ? "#" + toString(output.size()) : toHexPrefixed(output);

				// compare expected output with post output
				if (o.count("expectOut") > 0)
				{
					std::string warning = " Check State: Error! Unexpected output: " + o["out"].get_str() + " Expected: " + o["expectOut"].get_str();
					BOOST_CHECK_MESSAGE(o["out"].get_str() == o["expectOut"].get_str(), warning);
					o.erase(o.find("expectOut"));
				}

				o["gas"] = toCompactHexPrefixed(fev.gas, 1);
				o["logs"] = exportLog(fev.sub.logs);
			}
		}
		else
		{
			if (o.count("post") > 0)	// No exceptions expected
			{
				BOOST_CHECK(!vmExceptionOccured);

				BOOST_REQUIRE(o.count("post") > 0);
				BOOST_REQUIRE(o.count("callcreates") > 0);
				BOOST_REQUIRE(o.count("out") > 0);
				BOOST_REQUIRE(o.count("gas") > 0);
				BOOST_REQUIRE(o.count("logs") > 0);

				dev::test::FakeExtVM test(eth::EnvInfo{BlockHeader{}, lastBlockHashes, 0});
				test.importState(o["post"].get_obj());
				test.importCallCreates(o["callcreates"].get_array());
				test.sub.logs = importLog(o["logs"].get_array());


				checkOutput(output, o);

				BOOST_CHECK_EQUAL(toInt(o["gas"]), fev.gas);

				State postState(State::Null);
				State expectState(State::Null);
				mObject mPostState = fev.exportState();
				ImportTest::importState(mPostState, postState);
				ImportTest::importState(o["post"].get_obj(), expectState);
				ImportTest::compareStates(expectState, postState);

				//checkAddresses<std::map<Address, std::tuple<u256, u256, std::map<u256, u256>, bytes> > >(test.addresses, fev.addresses);

				checkCallCreates(fev.callcreates, test.callcreates);

				checkLog(fev.sub.logs, test.sub.logs);
			}
			else	// Exception expected
				BOOST_CHECK(vmExceptionOccured);
		}
	}

	TestOutputHelper::finishTest();
	return v;
}

} } // namespace close

BOOST_AUTO_TEST_SUITE(VMTests)

BOOST_AUTO_TEST_CASE(vmtests)
{
	dev::test::executeTests("vmtests", "/VMTests", "/VMTestsFiller", dev::test::doVMTests);
}

BOOST_AUTO_TEST_CASE(vmArithmeticTest)
{
	dev::test::executeTests("vmArithmeticTest", "/VMTests", "/VMTestsFiller", dev::test::doVMTests);
}

BOOST_AUTO_TEST_CASE(vmBitwiseLogicOperationTest)
{
	dev::test::executeTests("vmBitwiseLogicOperationTest", "/VMTests", "/VMTestsFiller", dev::test::doVMTests);
}

BOOST_AUTO_TEST_CASE(vmSha3Test)
{
	dev::test::executeTests("vmSha3Test", "/VMTests", "/VMTestsFiller", dev::test::doVMTests);
}

BOOST_AUTO_TEST_CASE(vmEnvironmentalInfoTest)
{
	dev::test::executeTests("vmEnvironmentalInfoTest", "/VMTests", "/VMTestsFiller", dev::test::doVMTests);
}

BOOST_AUTO_TEST_CASE(vmBlockInfoTest)
{
	dev::test::executeTests("vmBlockInfoTest", "/VMTests", "/VMTestsFiller", dev::test::doVMTests);
}

BOOST_AUTO_TEST_CASE(vmIOandFlowOperationsTest)
{
	dev::test::executeTests("vmIOandFlowOperationsTest", "/VMTests", "/VMTestsFiller", dev::test::doVMTests);
}

BOOST_AUTO_TEST_CASE(vmPushDupSwapTest)
{
	dev::test::executeTests("vmPushDupSwapTest", "/VMTests", "/VMTestsFiller", dev::test::doVMTests);
}

BOOST_AUTO_TEST_CASE(vmLogTest)
{
	dev::test::executeTests("vmLogTest", "/VMTests", "/VMTestsFiller", dev::test::doVMTests);
}

BOOST_AUTO_TEST_CASE(vmSystemOperationsTest)
{
	dev::test::executeTests("vmSystemOperationsTest", "/VMTests", "/VMTestsFiller", dev::test::doVMTests);
}

BOOST_AUTO_TEST_CASE(vmPerformanceTest)
{
	if (test::Options::get().performance)
		dev::test::executeTests("vmPerformanceTest", "/VMTests", "/VMTestsFiller", dev::test::doVMTests);
}

BOOST_AUTO_TEST_CASE(vmInputLimitsTest)
{
	if (test::Options::get().inputLimits)
		dev::test::executeTests("vmInputLimits", "/VMTests", "/VMTestsFiller", dev::test::doVMTests);
}

BOOST_AUTO_TEST_CASE(vmInputLimitsLightTest)
{
	if (test::Options::get().inputLimits)
		dev::test::executeTests("vmInputLimitsLight", "/VMTests", "/VMTestsFiller", dev::test::doVMTests);
}

BOOST_AUTO_TEST_CASE(vmRandom)
{
	test::Options::get(); // parse command line options, e.g. to enable JIT

	string testPath = getTestPath();
	testPath += "/VMTests/RandomTests";

	std::vector<boost::filesystem::path> testFiles = test::getJsonFiles(testPath);

	test::TestOutputHelper::initTest();
	test::TestOutputHelper::setMaxTests(testFiles.size());

	for (auto& path: testFiles)
	{
		try
		{
			cnote << "TEST " << path.filename();
			json_spirit::mValue v;
			string s = asString(dev::contents(path.string()));
			BOOST_REQUIRE_MESSAGE(s.length() > 0, "Content of " + path.string() + " is empty. Have you cloned the 'tests' repo branch develop and set ETHEREUM_TEST_PATH to its path?");
			json_spirit::read_string(s, v);
			test::Listener::notifySuiteStarted(path.filename().string());
			doVMTests(v, false);
		}
		catch (Exception const& _e)
		{
			BOOST_ERROR(" Failed test with Exception: " << diagnostic_information(_e));
		}
		catch (std::exception const& _e)
		{
			BOOST_ERROR(" Failed test with Exception: " << _e.what());
		}
	}
}

BOOST_AUTO_TEST_CASE(userDefinedFile)
{
	dev::test::userDefinedTest(dev::test::doVMTests);
}

BOOST_AUTO_TEST_SUITE_END()
