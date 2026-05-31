#include "config.h"

// paser parameters from config file
Parameters::Parameters(const std::string &config_file) {
  params_ptree = boost::property_tree::ptree();
  if (!config_file.empty()) boost::property_tree::ini_parser::read_ini(config_file, params_ptree);

  topology = params_ptree.get<std::string>("Network.topology", "SingleChipMesh");
  buffer_size = params_ptree.get<int>("Network.buffer_size", 16);
  vc_number = params_ptree.get<int>("Network.vc_number", 1);
  router_stages = params_ptree.get<std::string>("Network.router_stages", "ThreeStage");
  processing_time = params_ptree.get<int>("Network.processing_time", 2);
  credit_delay = params_ptree.get<int>("Network.credit_delay", 1);
  max_inflight_per_node = params_ptree.get<int>("Network.max_inflight_per_node", 0);

  traffic = params_ptree.get<std::string>("Workload.traffic", "uniform");
  traffic_scale = params_ptree.get<int>("Workload.traffic_scale", 0);
  packet_length = params_ptree.get<int>("Workload.packet_length", 5);
  node_id_remap = params_ptree.get<int>("Workload.node_id_remap", 0);
  region = params_ptree.get<int>("Workload.region", 0);
  run_all_regions = params_ptree.get<int>("Workload.run_all_regions", 0);
  disable_dependencies = params_ptree.get<int>("Workload.disable_dependencies", 1);

  injection_increment = params_ptree.get<double>("Simulation.injection_increment", 0.1);
  simulation_time = params_ptree.get<uint64_t>("Simulation.simulation_time", 10000);
  timeout_threshold = params_ptree.get<int>("Simulation.timeout_threshold", 500);
  timeout_limit = params_ptree.get<int>("Simulation.timeout_limit", 100);
  threads = params_ptree.get<int>("Simulation.threads", 2);
  if (threads >= 2)
    issue_width = params_ptree.get<int>("Simulation.issue_width", 10);
  else
    issue_width = 1000;

  if (traffic == "sd_traces")
    trace_file = params_ptree.get<std::string>("Files.trace_file");
  else if (traffic == "netrace")
    netrace_file = params_ptree.get<std::string>("Files.netrace_file");
  output_file = params_ptree.get<std::string>("Files.output_file", "../../output/output.csv");
  log_file = params_ptree.get<std::string>("Files.log_file", "../../output/log.txt");

  print_params();
}
