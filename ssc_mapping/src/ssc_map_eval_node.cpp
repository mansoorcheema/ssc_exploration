
// general includes
#include <assert.h>
#include <glog/logging.h>

#include <fstream>
#include <iostream>

// ros/pcl includes
#include <pcl/point_types.h>
#include <pcl_ros/point_cloud.h>
#include <ros/ros.h>

// voxblox includes
#include <ssc_mapping/visualization/visualization.h>
#include <voxblox/core/block.h>
#include <voxblox/core/common.h>
#include <voxblox/core/layer.h>
#include <voxblox/core/tsdf_map.h>
#include <voxblox/core/voxel.h>
#include <voxblox/io/layer_io.h>

using namespace voxblox;

bool isObserved(const voxblox::TsdfVoxel& voxel, float voxel_size) {
    return voxblox::visualizeOccupiedTsdfVoxels(voxel, voxblox::Point(), voxel_size);
}

bool isObserved(const voxblox::SSCOccupancyVoxel& voxel, float voxel_size) {
    voxblox::Color color;
    return voxblox::visualizeSSCOccupancyVoxels(voxel, voxblox::Point(), &color);
}

template <typename VoxelTypeA, typename VoxelTypeB>
void calculate_Intersection_difference(const voxblox::Layer<VoxelTypeA>& layer,
                                       const voxblox::Layer<VoxelTypeB>& otherLayer, GlobalIndexVector* intersection,
                                       GlobalIndexVector* difference) {
    size_t vps = layer.voxels_per_side();
    size_t num_voxels_per_block = vps * vps * vps;

    voxblox::BlockIndexList blocks;
    layer.getAllAllocatedBlocks(&blocks);
    for (const voxblox::BlockIndex& index : blocks) {
        // Iterate over all voxels in said blocks.
        const voxblox::Block<voxblox::TsdfVoxel>& block = layer.getBlockByIndex(index);

        for (size_t linear_index = 0; linear_index < num_voxels_per_block; ++linear_index) {
            // voxblox::Point coord = block.computeCoordinatesFromLinearIndex(linear_index);
            auto gt_voxel = block.getVoxelByLinearIndex(linear_index);

            if (isObserved(gt_voxel, layer.voxel_size())) {
                // voxel is observed in first layer. check if it exists in other layer

                // get global voxel index
                voxblox::VoxelIndex voxel_idx = block.computeVoxelIndexFromLinearIndex(linear_index);
                voxblox::BlockIndex block_idx = index;
                voxblox::GlobalIndex global_voxel_idx =
                    voxblox::getGlobalVoxelIndexFromBlockAndVoxelIndex(block_idx, voxel_idx, layer.voxels_per_side());

                // see if this voxel is observed in other layer
                auto observed_voxel = otherLayer.getVoxelPtrByGlobalIndex(global_voxel_idx);

                if (observed_voxel != nullptr && isObserved(*observed_voxel, otherLayer.voxel_size())) {
                    intersection->emplace_back(global_voxel_idx);
                } else {
                    difference->emplace_back(global_voxel_idx);
                }
            }
        }
    }
}

// Creates a pointcloud from voxel indices
void createPointCloudFromVoxelIndices(const voxblox::GlobalIndexVector& voxels,
                                      pcl::PointCloud<pcl::PointXYZRGB>* pointcloud, const voxblox::Color& color, const float voxel_size = 0.08) {
    for (auto voxel : voxels) {
        pcl::PointXYZRGB point;
        point.x = voxel.x() * voxel_size + (voxel_size/2);
        point.y = voxel.y() * voxel_size + (voxel_size/2);
        point.z = voxel.z() * voxel_size + (voxel_size/2);
        point.r = color.r;
        point.g = color.g;
        point.b = color.b;
        pointcloud->push_back(point);
    }
    pointcloud->header.frame_id = "world";
}

// unit_test
void test_eval_metrics() {
    voxblox::TsdfMap::Ptr ground_truth_map;
    voxblox::TsdfMap::Ptr observed_map;
    voxblox::TsdfMap::Config config;
    config.tsdf_voxel_size = 1;
    config.tsdf_voxels_per_side = 8u;

    ground_truth_map = std::make_shared<voxblox::TsdfMap>(config);
    observed_map = std::make_shared<voxblox::TsdfMap>(config);

    {
        // add a block at origin
        voxblox::Point point_in_0_0_0(0, 0, 0);
        voxblox::Block<voxblox::TsdfVoxel>::Ptr block_0_0_0 =
            ground_truth_map->getTsdfLayerPtr()->allocateNewBlockByCoordinates(point_in_0_0_0);
        observed_map->getTsdfLayerPtr()->allocateNewBlockByCoordinates(point_in_0_0_0);

        // add another block
        voxblox::Point point_in_10_0_0(ground_truth_map->block_size(), 0, 0);
        voxblox::Block<voxblox::TsdfVoxel>::Ptr block_1_0_0 =
            ground_truth_map->getTsdfLayerPtr()->allocateNewBlockByCoordinates(point_in_10_0_0);

        {
            int gt_x_min = 4;
            int gt_x_max = 12;
            int gt_y_min = 3;
            int gt_y_max = 6;

            for (size_t x = gt_x_min; x < gt_x_max; x++) {
                for (size_t y = gt_y_min; y < gt_y_max; y++) {
                    // global voxel by global index
                    // auto voxel =
                    voxblox::GlobalIndex voxelIdx(x, y, 0);

                    auto voxel = ground_truth_map->getTsdfLayerPtr()->getVoxelPtrByGlobalIndex(voxelIdx);
                    voxel->weight = 1;
                }
            }

            int observed_x_min = 5;
            int observed_x_max = 8;
            int observed_y_min = 2;
            int observed_y_max = 4;

            // add voxels to observed map
            for (size_t x = observed_x_min; x < observed_x_max; x++) {
                for (size_t y = observed_y_min; y < observed_y_max; y++) {
                    // global voxel by global index
                    // auto voxel =
                    voxblox::GlobalIndex voxelIdx(x, y, 0);

                    auto voxel = observed_map->getTsdfLayerPtr()->getVoxelPtrByGlobalIndex(voxelIdx);
                    voxel->weight = 1;
                }
            }
        }
    }

    voxblox::GlobalIndexVector intersection_gt, difference_gt;
    calculate_Intersection_difference(ground_truth_map->getTsdfLayer(), observed_map->getTsdfLayer(), &intersection_gt,
                                      &difference_gt);

    CHECK(intersection_gt.size() == 3) << "Error in Intersection Algorithm! Recheck Implementation.";
    CHECK(difference_gt.size() == 21) << "Error in DIfference Algorithm! Recheck Implementation.";
}

int main(int argc, char** argv) {
    ros::init(argc, argv, "ssc_mapping_eval");
    google::InitGoogleLogging(argv[0]);
    google::InstallFailureSignalHandler();
    google::ParseCommandLineFlags(&argc, &argv, false);

    ros::NodeHandle nh("");
    ros::NodeHandle nh_private("~");

    test_eval_metrics();

    voxblox::Layer<voxblox::TsdfVoxel>::Ptr ground_truth_layer;
    voxblox::Layer<voxblox::TsdfVoxel>::Ptr observed_layer;

    voxblox::TsdfMap::Ptr ground_truth_map;
    voxblox::TsdfMap::Ptr observed_map;

    // load ground truth and observed map from file

    if (argc < 3) {
        std::cout << "Usage: rosrun ssc_map_eval_node <gt_layer> <observed_layer> <optional_output> "
                     "<optional_publish_stats>";
        return -1;
    }

    voxblox::io::LoadLayer<voxblox::TsdfVoxel>(argv[1], &ground_truth_layer);
    voxblox::io::LoadLayer<voxblox::TsdfVoxel>(argv[2], &observed_layer);

    ground_truth_map = std::make_shared<voxblox::TsdfMap>(ground_truth_layer);
    observed_map = std::make_shared<voxblox::TsdfMap>(observed_layer);

    CHECK(ground_truth_map->getTsdfLayerPtr()->voxel_size() == observed_map->getTsdfLayerPtr()->voxel_size())
        << "Error! Observed Layer and groundtruth layers should have same voxel size!";

    voxblox::GlobalIndexVector intersection_gt, difference_gt;
    calculate_Intersection_difference(ground_truth_map->getTsdfLayer(), observed_map->getTsdfLayer(), &intersection_gt,
                                      &difference_gt);

    voxblox::GlobalIndexVector intersection_observed, difference_observed;
    calculate_Intersection_difference(observed_map->getTsdfLayer(), ground_truth_map->getTsdfLayer(),
                                      &intersection_observed, &difference_observed);

    // compute evaluation metrics
    size_t gt_occupied_voxels = 0;
    size_t observed_voxels = 0;
    float precision = 0.0f;
    float recall = 0.0f;
    float iou = 0;
    float observed_region = 0;

    observed_voxels = intersection_observed.size() + difference_observed.size();
    gt_occupied_voxels = difference_gt.size() + intersection_gt.size();
    iou = intersection_gt.size() / float(intersection_gt.size() + difference_gt.size() + difference_observed.size());
    recall = intersection_observed.size() / float(intersection_gt.size() + difference_gt.size());
    precision = intersection_observed.size() / float(intersection_observed.size() + difference_observed.size());
    observed_region = (observed_voxels / float(gt_occupied_voxels));

    // print eval statis
    printf("---------- Evaluation -----------\n");
    printf("iou: %0.2lf \n", iou);
    printf("precision: %0.2lf \n", precision);
    printf("recall: %0.2lf \n", recall);
    printf("observed: %0.2lf\n", observed_region);
    // printf("gt_occupied_voxels: %d\n", gt_occupied_voxels);
    // printf("observed_voxels: %d\n",observed_voxels);
    printf("---------------------------------\n");

    if (argc > 3) {  // output to file
        std::ofstream file(argv[3], std::ios::app);

        if (file.is_open()) {
            file << observed_region << "," << iou << "," << precision << "," << recall << std::endl;
            file.close();
        }

        else
            std::cerr << "Unable to open " << argv[3] << " file";
    }

    if (argc > 4 && std::string("publish").compare(argv[4]) == 0) {  // publish

        auto missed_occupancy_voxels_pub =
            nh_private.advertise<pcl::PointCloud<pcl::PointXYZRGB> >("occupancy_pointcloud_diff", 1, true);
        auto correct_occupied_voxels_observed_pub =
            nh_private.advertise<pcl::PointCloud<pcl::PointXYZRGB> >("occupancy_pointcloud_inter", 1, true);
        auto false_positive_observations_pub =
            nh_private.advertise<pcl::PointCloud<pcl::PointXYZRGB> >("false_positive_observations", 1, true);

        std::cout << "Publishing voxels comparison!" << std::endl;
        pcl::PointCloud<pcl::PointXYZRGB> pointcloud_diff;
        createPointCloudFromVoxelIndices(difference_gt, &pointcloud_diff, Color::Red());
        missed_occupancy_voxels_pub.publish(pointcloud_diff);

        pcl::PointCloud<pcl::PointXYZRGB> pointcloud_inter;
        createPointCloudFromVoxelIndices(intersection_gt, &pointcloud_inter, Color::Green());
        correct_occupied_voxels_observed_pub.publish(pointcloud_inter);

        pcl::PointCloud<pcl::PointXYZRGB> pointcloud_observed_occupancy_fp;
        createPointCloudFromVoxelIndices(difference_observed, &pointcloud_observed_occupancy_fp, Color::Yellow());
        false_positive_observations_pub.publish(pointcloud_observed_occupancy_fp);

        ros::spin();
    }

    return 0;
}