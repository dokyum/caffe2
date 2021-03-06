#ifndef CAFFE2_CORE_OPERATOR_SCHEMA_H_
#define CAFFE2_CORE_OPERATOR_SCHEMA_H_

#include <climits>
#include <functional>
#include <initializer_list>
#include <ostream>
#include <set>
#include <vector>

#include "caffe2/core/common.h"
#include "caffe2/core/logging.h"
#include "caffe2/core/registry.h"
#include "caffe2/proto/caffe2.pb.h"

namespace caffe2 {

// A const value returned by OpSchema::CalculateOutput() if the number of
// output cannot be determined.
constexpr int kCannotComputeNumOutputs = -1;

/**
 * @brief A class to record the schema of an op.
 *
 * OpSchema records the common interface of an op specified by its name. This
 * is optional for each operator implemented in Caffe2 but is strongly
 * recommended.
 *
 * To register an OpSchema, one can use the macro OPERATOR_SCHEMA(name) and
 * then append the various functions in the class. For example, for an op
 * that itakes in two inputs, one output, and the first input and output
 * could be in-place, can be written as
 *
 *     OPERATOR_SCHEMA(name)
 *         .NumInputs(2).NumOutputs(1).AllowInplace({{0, 0}});
 */
class OpSchema {
 public:
  OpSchema() : file_("unknown"), line_(0) {}
  OpSchema(const string& file, const int line)
      : file_(file), line_(line) {}

  /**
   * @brief Returns the file that the op schema is registered from.
   */
  inline const string& file() const { return file_; }

  /**
   * @brief Returns the line in file that the op schema is registered from.
   */
  inline int line() const { return line_; }

  /**
   * @brief Returns the docstring of the op schema.
   */
  inline const char* doc() const {
    return doc_.empty() ? nullptr : doc_.c_str();
  }

  /**
   * @brief Verifies if an operator definition protobuf matches the pattern
   * specified in the schema.
   */
  bool Verify(const OperatorDef& def) const;

  // Functions to set the property of the operator schemas.
  // Sets the number of inputs, either a fixed number or a min and a max.

  /**
   * @brief A single input.
   */
  OpSchema& NumInputs(int n);
  /**
   * @brief Input could be in range [min, max], inclusive.
   */
  OpSchema& NumInputs(int min, int max);
  /**
   * @brief Input could be one of the values specified in allowed_input_nums.
   */
  OpSchema& NumInputs(set<int> allowed_input_nums);
  /**
   * @brief Input is checked with a specified function.
   */
  OpSchema& NumInputs(std::function<bool(int)> func);

  // Sets the number of outputs, either a fixed number, a min and a max,
  // or a function that takes in the input number and produces an output
  // number. Use only one function in the set below.
  /**
   * @brief A single output.
   */
  OpSchema& NumOutputs(int n);
  /**
   * @brief Output could be in range [min, max], inclusive.
   */
  OpSchema& NumOutputs(int min, int max);
  /**
   * @brief Output could be one of the values specified in allowed_output_nums.
   */
  OpSchema& NumOutputs(set<int> allowed_output_nums);
  /**
   * @brief Output is checked with a specified function.
   */
  OpSchema& NumOutputs(std::function<bool(int)> func);

  /**
   * @brief Relationship between inputs and outputs is checked with a specified
   * function.
   */
  OpSchema& NumInputsOutputs(std::function<bool(int, int)> func);

  // Set the function that can calculate the number of output based on the
  // number of input. Use only one function in the set below.
  /**
   * @brief Set the output calculator to a user-defined function.
   */
  OpSchema& OutputCalculator(std::function<int(int)> calc);
  /**
   * @brief Set the number of outputs to be the same as the number of inputs.
   */
  OpSchema& SameNumberOfOutput();

  // Sets the rule to allow optional in-place operation.
  OpSchema& AllowInplace(std::function<bool(int, int)> inplace);
  OpSchema& AllowInplace(set<std::pair<int, int>> inplace);
  OpSchema& AllowOneToOneInplace();
  // Sets the rule to enforce in-place opeartion.
  OpSchema& EnforceInplace(std::function<bool(int, int)> inplace);
  OpSchema& EnforceInplace(set<std::pair<int, int>> inplace);
  OpSchema& EnforceOneToOneInplace();

  // Functions to deal with type and shape inference. Basically, this registers
  // a function that takes in an OperatorDef and a series of input type and
  // shape specified by TensorProto objects (whose data fields are empty), and
  // produces a series of output type and shape.
  typedef std::function<
      vector<TensorShape>(const OperatorDef&, const vector<TensorShape>&)>
      TensorInferenceFunctionType;
  /**
   * @brief Sets the tensor inference function, which is a std::function object
   * defined in operator_schema.h.
   */
  OpSchema& TensorInferenceFunction(TensorInferenceFunctionType function);
  /**
   * @brief Seets the tensor inference function to produce the same output as
   * the input.
   */
  OpSchema& IdenticalTypeAndShape();
  OpSchema& IdenticalTypeAndShapeOfInput(int idx);
  OpSchema& IdenticalTypeAndShapeOfInputDim(int idx, int dim);
  OpSchema& ScalarType(::caffe2::TensorProto_DataType dt);

  /**
   * @brief A function to allow one to infer the type and shape from the op
   * schema.
   */
  inline vector<TensorShape> InferTensor(
      const OperatorDef& def,
      const vector<TensorShape> input_type_shape) const {
    return tensor_inference_function_(def, input_type_shape);
  }

  /*
   * @brief A struct to store various cost information about
   * an operator such as FLOPs and total memory use.
   */
  struct Cost {
    size_t flops; // Floating point operations.
    size_t bytes_moved; // Total memory used.
  };
  /**
   * @brief Registers a function that takes in an OperatorDef
   * and a series of input shapes and returns the total "cost"
   * required to run the operator via struct by value.
   */
  typedef std::function<
      struct Cost(const OperatorDef&, const vector<TensorShape>&)>
      CostInferenceFunctionType;

  /**
   * @brief Register the Cost inference function.
   */
  OpSchema& CostInferenceFunction(CostInferenceFunctionType function);
  inline struct Cost InferCost(
      const OperatorDef& def,
      const vector<TensorShape>& input_tensor_shape) const {
    return cost_inference_function_(def, input_tensor_shape);
  }

  // Functions to do documentation for the operator schema.
  OpSchema& SetDoc(const string& doc);
  OpSchema& Arg(const char* name, const char* description);
  OpSchema& Input(const int n, const char* name, const char* description);
  OpSchema& Output(const int n, const char* name, const char* description);
  // Calls the passed function with `this` as an argument. Useful for
  // adding docs for temlated/macro ops.
  OpSchema& FillUsing(std::function<void(OpSchema&)> populator);

  // Remove from documentation
  OpSchema& Private();

  // This op can pass data across devices
  OpSchema& InputsCanCrossDevices();

  /**
   * @brief A function to allow one to get the number of outputs based on the
   * number of inputs, if this schema supports it.
   */
  int CalculateOutput(int num_input) const;

  friend std::ostream& operator<<(std::ostream& out, const OpSchema& schema);

  const std::vector<std::pair<const char*, const char*>>& arg_desc() {
    return arg_desc_;
  }
  const std::vector<std::pair<const char*, const char*>>& input_desc() {
    return input_desc_;
  }
  const std::vector<std::pair<const char*, const char*>>& output_desc() {
    return output_desc_;
  }
  bool private_op() {
    return private_;
  }
  bool inputs_can_cross_devices() const {
    return inputs_can_cross_devices_;
  }

  /**
   * @brief Returns the required device location of inputs and outputs.
   */
  using DeviceInferenceFunctionType = std::function<
      std::pair<std::vector<DeviceOption>, std::vector<DeviceOption>>(
          const OperatorDef& def)>;

  OpSchema& DeviceInferenceFunction(DeviceInferenceFunctionType function);

  /**
   * @brief Infer required device location of an op's inputs and outputs
   */
  inline std::pair<std::vector<DeviceOption>, std::vector<DeviceOption>>
  InferDevice(const OperatorDef& def) const {
    return device_inference_function_(def);
  }

 private:
  [[noreturn]] Cost DefaultConstInferenceFunction(
      const OperatorDef&,
      const vector<TensorShape>&) {
    CAFFE_THROW("No cost inference function registered.");
  }

 private:
  string file_;
  string doc_;
  std::vector<std::pair<const char*, const char*>> arg_desc_{};
  std::vector<std::pair<const char*, const char*>> input_desc_{};
  std::vector<std::pair<const char*, const char*>> output_desc_{};
  int line_ = 0;
  int min_input_ = 0;
  int max_input_ = std::numeric_limits<int>::max();
  int min_output_ = 0;
  int max_output_ = std::numeric_limits<int>::max();
  bool private_ = false;
  bool inputs_can_cross_devices_ = false;
  std::function<bool(int)> num_inputs_allowed_
      = [](int) { return true; };
  std::function<bool(int)> num_outputs_allowed_
      = [](int) { return true; };
  std::function<bool(int, int)> num_inputs_outputs_allowed_
      = [](int, int) { return true; };
  std::function<int(int)> calculate_output_;
  // In default, any in-place operation is neither allowed nor enforced.
  std::function<bool(int, int)> inplace_allowed_
      = [](int, int) { return false; };
  std::function<bool(int, int)> inplace_enforced_
      = [](int, int) { return false; };
  TensorInferenceFunctionType tensor_inference_function_ =
      [](const OperatorDef& def, const vector<TensorShape>&) {
        vector<TensorShape> out;
        for (int i = 0; i < def.output_size(); i++) {
          TensorShape ts;
          ts.set_unknown_shape(true);
          out.push_back(ts);
        }
        return out;
      };
  CostInferenceFunctionType cost_inference_function_ = std::bind(
      &OpSchema::DefaultConstInferenceFunction,
      this,
      std::placeholders::_1,
      std::placeholders::_2);
  DeviceInferenceFunctionType device_inference_function_ =
      [](const OperatorDef& def) {
        auto op_device =
            def.has_device_option() ? def.device_option() : DeviceOption();
        vector<DeviceOption> in_dev(def.input_size(), op_device);
        vector<DeviceOption> out_dev(def.output_size(), op_device);
        return std::make_pair(in_dev, out_dev);
      };
};

/**
 * @brief A registry to hold all the operator schemas.
 */
class OpSchemaRegistry {
 public:
  static OpSchema& NewSchema(
      const string& key, const string& file, const int line) {
    auto& m = map();
    if (m.count(key)) {
      const auto& schema = m[key];
      std::ios_base::Init init;
      std::cerr << "Trying to register schema with name "
                << key << " from file " << file << " line " << line
                << ", but it is already registered from file "
                << schema.file() << " line " << schema.line();
      abort();
    }
    m.emplace(std::make_pair(key, OpSchema(file, line)));
    return m[key];
  }

  static const OpSchema* Schema(const string& key) {
    auto& m = map();
    if (m.count(key)) {
      return &m[key];
    } else {
      return nullptr;
    }
  }

 private:
  // OpSchemaRegistry should not need to be instantiated.
  OpSchemaRegistry() = delete;

  /**
   * @brief Returns the underlying string to OpSchema map.
   *
   * You should not manually manipulate the map object returned. Instead, use
   * the macros defined such as OPERATOR_SCHEMA to register your operator
   * schema.
   *
   * We wrap it inside a function to avoid the statia initialization order
   * fiasco.
   */
  static CaffeMap<string, OpSchema>& map();
};

// Helper function for creating simple tensorproto with dimension and type
inline TensorShape CreateTensorShape(
    vector<int> dims,
    ::caffe2::TensorProto_DataType dt) {
  TensorShape ts;
  for (int d : dims) {
    ts.add_dims(d);
  }
  ts.set_data_type(dt);
  return ts;
}

// Helper function
inline vector<TIndex> GetDimsVector(const TensorShape& shape) {
  vector<TIndex> dims;
  for (auto d : shape.dims()) {
    dims.push_back(d);
  }
  return dims;
}

// Helper function for infer op inputs and outputs device information.
inline std::pair<std::vector<DeviceOption>, std::vector<DeviceOption>>
InferOpInputOutputDevice(const OperatorDef& op) {
  auto op_schema = OpSchemaRegistry::Schema(op.type());
  CAFFE_ENFORCE(
      op_schema, "Device inference failed. No schema for: ", op.type());
  // TODO(wyiming) : add try catch here.
  return op_schema->InferDevice(op);
}

}  // namespace caffe2

#ifndef CAFFE2_NO_OPERATOR_SCHEMA

#define OPERATOR_SCHEMA(name)                            \
  void CAFFE2_PLEASE_ADD_OPERATOR_SCHEMA_FOR_##name(){}; \
  static OpSchema* CAFFE_ANONYMOUS_VARIABLE(name) =      \
      &OpSchemaRegistry::NewSchema(#name, __FILE__, __LINE__)
#define OPERATOR_SCHEMA_STR(name)                                  \
  static OpSchema* CAFFE_ANONYMOUS_VARIABLE(schema_registration) = \
      &OpSchemaRegistry::NewSchema(name, __FILE__, __LINE__)

#else // CAFFE2_NO_OPERATOR_SCHEMA

#define OPERATOR_SCHEMA(name)                            \
  void CAFFE2_PLEASE_ADD_OPERATOR_SCHEMA_FOR_##name(){}; \
  static OpSchema* CAFFE_ANONYMOUS_VARIABLE(name) =      \
      1 ? nullptr : &OpSchemaRegistry::NewSchema(#name, __FILE__, __LINE__)
#define OPERATOR_SCHEMA_STR(name)                                  \
  static OpSchema* CAFFE_ANONYMOUS_VARIABLE(schema_registration) = \
      1 ? nullptr : &OpSchemaRegistry::NewSchema(name, __FILE__, __LINE__)

#endif // CAFFE2_NO_OPERATOR_SCHEMA

#endif  // CAFFE2_CORE_OPERATOR_SCHEMA_H_
