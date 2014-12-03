#include "barf.hpp"

// This must be included exactly once in only this file!!!
#include "runtime.inc"

// Please keep the predicates in alphabetical order.
namespace barf {

/**
 * A predicate that checks of the read is paired.
 */
class is_paired_node : public ast_node {
	public:
	virtual llvm::Value *generate(llvm::Module *module, llvm::IRBuilder<> builder, llvm::Value *read, llvm::Value *header) {
		auto function = define_is_paired(module);
		return builder.CreateCall(function, read);
	}
};

static std::shared_ptr<ast_node> parse_is_paired(std::string input, int&index) throw (parse_error) {
	static auto result = std::make_shared<is_paired_node>();
	return result;
}

/**
 * A predicate that always return true.
 */
class true_node : public ast_node {
	public:
	virtual llvm::Value *generate(llvm::Module *module, llvm::IRBuilder<> builder, llvm::Value *read, llvm::Value *header) {
		return llvm::ConstantInt::getTrue(llvm::getGlobalContext());
	}
};

static std::shared_ptr<ast_node> parse_true(std::string input, int&index) throw (parse_error) {
	static auto result = std::make_shared<true_node>();
	return result;
}

/**
 * All the predicates known to the system.
 */
predicate_map getDefaultPredicates() {
	return  {
		{std::string("is_paired"), parse_is_paired},
		{std::string("true"), parse_true}
	};
}


llvm::Type *getBamType(llvm::Module *module) {
	auto struct_bam1_t = module->getTypeByName("struct.bam1_t");
	if (struct_bam1_t == nullptr) {
		define___dummy__(module);
	}
	return module->getTypeByName("struct.bam1_t");
}

llvm::Type *getBamHeaderType(llvm::Module *module) {
	auto struct_bam1_t = module->getTypeByName("struct.bam_hdr_t");
	if (struct_bam1_t == nullptr) {
		define___dummy__(module);
	}
	return module->getTypeByName("struct.bam_hdr_t");
}
}
