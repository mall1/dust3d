#include "imgui.h" // necessary for ImGui::*, imgui-SFML.h doesn't include imgui.h

#include "imgui-SFML.h" // for ImGui::SFML::* functions and SFML-specific overloads

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/Event.hpp>
#include "SFML/Graphics/Texture.hpp"
#include <iostream>
#include "dust3d/base/ds3_file.h"
#include <dust3d/base/snapshot.h>
#include <dust3d/base/snapshot_xml.cc>
#include <fstream>

std::vector<sf::Texture> textures;
void test()
{
    //QFile file(path);
    //file.open(QFile::ReadOnly);
    //QByteArray fileData = file.readAll();
    std::string filepath = TEST_DS3_FILE;
    std::ifstream file(filepath, std::ios::in | std::ios::binary);
    if (!file.is_open())
        return ;

    // Read contents
    std::string fileData { std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>() };

    // Close the file
    file.close();

    dust3d::Ds3FileReader ds3Reader((const std::uint8_t*)fileData.data(), fileData.size());
    for (int i = 0; i < (int)ds3Reader.items().size(); ++i) {
        const dust3d::Ds3ReaderItem& item = ds3Reader.items()[i];
        std::cout << "[" << i << "]item.name:" << item.name << "item.type:" << item.type << std::endl;
        if (item.type == "asset") {
            if (dust3d::String::startsWith(item.name, "images/")) {
                std::string filename = dust3d::String::split(item.name, '/')[1];
                std::string imageIdString = dust3d::String::split(filename, '.')[0];
                dust3d::Uuid imageId = dust3d::Uuid(imageIdString);
                if (!imageId.isNull()) {
                    std::vector<std::uint8_t> data;
                    ds3Reader.loadItem(item.name, &data);
                    sf::Texture texture;
                    texture.loadFromMemory(data.data(), (int)data.size()); 
                    textures.push_back(texture);
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

int main() {
    sf::RenderWindow window(sf::VideoMode(1280, 720), "ImGui + SFML = <3");
    window.setFramerateLimit(60);
    ImGui::SFML::Init(window);

    sf::RenderWindow childWindow(sf::VideoMode(640, 480), "ImGui-SFML Child window");
    childWindow.setFramerateLimit(60);
    ImGui::SFML::Init(childWindow);

    sf::Clock deltaClock;

    
		test();
    while (window.isOpen()) {
        // Main window event processing
        sf::Event event;
        while (window.pollEvent(event)) {
            ImGui::SFML::ProcessEvent(window, event);
            if (event.type == sf::Event::Closed) {
                if (childWindow.isOpen()) {
                    childWindow.close();
                }
                window.close();
                ImGui::SFML::Shutdown(); // will shutdown all windows
                return 0; // return here so that we don't call Update/Render
            }
        }

        // Child window event processing
        if (childWindow.isOpen()) {
            while (childWindow.pollEvent(event)) {
                ImGui::SFML::ProcessEvent(childWindow, event);
                if (event.type == sf::Event::Closed) {
                    childWindow.close();
                    ImGui::SFML::Shutdown(childWindow);
                }
            }
        }

        // Update
        const sf::Time dt = deltaClock.restart();
        ImGui::SFML::Update(window, dt);
        if (childWindow.isOpen()) {
            ImGui::SFML::Update(childWindow, dt);
        }

        // Add ImGui widgets in the first window
        ImGui::SFML::SetCurrentWindow(window);
        ImGui::Begin("Hello, world!");
        ImGui::Button("Look at this pretty button");
        
        for (auto& texture : textures) {
            ImGui::Image(texture);
        }
		
        ImGui::End();
        ImGui::ShowDemoWindow();
        // Add ImGui widgets in the child window
        if (childWindow.isOpen()) {
            ImGui::SFML::SetCurrentWindow(childWindow);
            ImGui::Begin("Works in a second window!");
            ImGui::Button("Example button");
            ImGui::End();
        }

        // Main window drawing
        sf::CircleShape shape(100.f);
        shape.setFillColor(sf::Color::Green);

        window.clear();
        window.draw(shape);
        ImGui::SFML::Render(window);
        window.display();

        // Child window drawing
        if (childWindow.isOpen()) {
            sf::CircleShape shape2(50.f);
            shape2.setFillColor(sf::Color::Red);

            childWindow.clear();
            childWindow.draw(shape2);
            ImGui::SFML::Render(childWindow);
            childWindow.display();
        }
    }

    return 0;
}
