/*
 * @Author: shysgsg 1054733568@qq.com
 * @Date: 2025-09-05 13:27:49
 * @LastEditors: shysgsg 1054733568@qq.com
 * @LastEditTime: 2025-09-05 23:26:02
 * @FilePath: \AeroLink\main.cpp
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#include <QApplication>
#include <QImage>
#include <iostream>
#include <vector>
#include "mainwindow.h"
#include "logmanager.h"
#include "AuxFileReader.h"
#include "package_sar_data.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // The call to LogManager::instance() will automatically install the message handler
    LogManager::instance();

    MainWindow w;
    w.show();

    std::string aux_filename = "D:/xwechat_files/wxid_um3cz3st1tjt22_48d0/msg/file/2025-08/Harbin_0713_PGA/Harbin_0713_PGA/AUX_W_H1H1_ID1_offset500000_v2.dat";
    std::string image_filename = "D:/xwechat_files/wxid_um3cz3st1tjt22_48d0/msg/file/2025-08/Harbin_0713_PGA/Harbin_0713_PGA/data_pga_all_itr_complete12_23437x4096_3349x4096.tif";
    AuxFileReader reader;

    if (reader.read(aux_filename)) {
        // Call print functions after successful read
        reader.printHeader();
        reader.printData();

        // 1. Define output filename for the packaged data
        std::string packaged_filename = "E:/Qt_projects/AeroLink/data/packaged_sar_data.bin";

        // 2. Prepare SAR_DataInfo struct using the data from the AUX file
        SAR_DataInfo my_data_info = createSarDataInfo(reader.getHeader());

        // 3. 从 image 文件读取图像数据
        std::vector<uint8_t> my_image_data;
        {
            std::ifstream image_file(image_filename, std::ios::binary);
            if (image_file) {
                image_file.seekg(0, std::ios::end);
                size_t file_size = image_file.tellg();
                image_file.seekg(0, std::ios::beg);
                my_image_data.resize(file_size);
                image_file.read(reinterpret_cast<char*>(my_image_data.data()), file_size);
            } else {
                std::cerr << "无法打开图像文件: " << image_filename << std::endl;
                return -1;
            }
        }

        // 4. Define image number
        uint16_t my_image_number = 1;

        // 5. Call the packaging function
        std::cout << "Packaging SAR data...\n";
        bool package_success = package_sar_data(packaged_filename, my_data_info, my_image_data, my_image_number);

        // 6. Check the packaging result
        if (package_success) {
            std::cout << "SAR data successfully packaged to file: " << packaged_filename << std::endl;

            // 7. Define output filename for the unpacked image
            std::string unpacked_image_filename = "E:/Qt_projects/AeroLink/data/unpacked_image.tif";

            // 8. Call the unpacking function to verify
            std::cout << "\nUnpacking SAR data...\n";
            bool unpack_success = unpackage_sar_data(packaged_filename, unpacked_image_filename);

            // 9. Check the unpacking result
            if (unpack_success) {
                std::cout << "SAR data successfully unpacked! Image saved to: " << unpacked_image_filename << std::endl;
                std::cout << "Please check the image file to verify the data integrity.\n";
            } else {
                std::cerr << "SAR data unpacking failed!\n";
            }

        } else {
            std::cerr << "SAR data packaging failed.\n";
        }
    } else {
        std::cerr << "Failed to read AUX file. Packaging/Unpacking tests will not be performed." << std::endl;
    }

    return a.exec();
}
