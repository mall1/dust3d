#include <iostream>
#include "dust3d/base/ds3_file.h"
#include <dust3d/base/snapshot.h>
#include <dust3d/base/snapshot_xml.cc>
#include <fstream>
int main(){
    //QFile file(path);
    //file.open(QFile::ReadOnly);
    //QByteArray fileData = file.readAll();
    std::string filepath = "C:/Users/mall/Desktop/aaa.ds3";
    std::ifstream file(filepath, std::ios::in | std::ios::binary);
    if (!file.is_open())
        return 0;

    // Read contents
    std::string fileData { std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>() };

    // Close the file
    file.close();
	
    dust3d::Ds3FileReader ds3Reader((const std::uint8_t*)fileData.data(), fileData.size());
    for (int i = 0; i < (int)ds3Reader.items().size(); ++i) {
        const dust3d::Ds3ReaderItem& item = ds3Reader.items()[i];
        std::cout << "[" << i << "]item.name:" << item.name << "item.type:" << item.type<<std::endl;
        if (item.type == "asset") {
            if (dust3d::String::startsWith(item.name, "images/")) {
                std::string filename = dust3d::String::split(item.name, '/')[1];
                std::string imageIdString = dust3d::String::split(filename, '.')[0];
                dust3d::Uuid imageId = dust3d::Uuid(imageIdString);
                if (!imageId.isNull()) {
                    std::vector<std::uint8_t> data;
                    ds3Reader.loadItem(item.name, &data);
                    //QImage image = QImage::fromData(data.data(), (int)data.size(), "PNG");
                    //(void)ImageForever::add(&image, imageId);
                }
            }
        }
    }

    for (int i = 0; i < (int)ds3Reader.items().size(); ++i) {
        const dust3d::Ds3ReaderItem& item = ds3Reader.items()[i];
        if (item.type == "model") {
            std::vector<std::uint8_t> data;
            ds3Reader.loadItem(item.name, &data);
            data.push_back('\0');
            dust3d::Snapshot snapshot;
            loadSnapshotFromXmlString(&snapshot, (char*)data.data());
            //m_document->fromSnapshot(snapshot);
            //m_document->saveSnapshot();
        } else if (item.type == "asset") {
            if (item.name == "canvas.png") {
                std::vector<std::uint8_t> data;
                ds3Reader.loadItem(item.name, &data);
                //m_document->updateTurnaround(QImage::fromData(data.data(), (int)data.size(), "PNG"));
            }
        }
    }
}