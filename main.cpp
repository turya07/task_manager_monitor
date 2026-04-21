#include <SFML/Graphics.hpp>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <deque>
#include <sstream>
#include <array>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <thread>
#include <unordered_map>

const unsigned int range = 20;

#define FG_COL sf::Color(0xddddddff)
#define BG_COL sf::Color(0x101010ff)

struct Process
{
    int pid{};
    std::string name;
    float cpuUsage{};
};

static std::string trim(const std::string &value)
{
    const auto start = value.find_first_not_of(" \t");
    if (start == std::string::npos)
        return {};
    const auto end = value.find_last_not_of(" \t");
    return value.substr(start, end - start + 1);
}

static bool isNumericPid(const std::string &name)
{
    return !name.empty() &&
           std::all_of(name.begin(), name.end(), [](unsigned char ch)
                       { return std::isdigit(ch); });
}

static bool readProcessName(int pid, std::string &name)
{
    std::ifstream status("/proc/" + std::to_string(pid) + "/status");
    if (!status)
        return false;

    std::string line;
    while (std::getline(status, line))
    {
        if (line.rfind("Name:", 0) == 0)
        {
            name = trim(line.substr(5));
            return !name.empty();
        }
    }
    return false;
}

static unsigned long long readTotalJiffies()
{
    std::ifstream stat("/proc/stat");
    if (!stat)
        return 0;

    std::string cpuTag;
    unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0;
    unsigned long long irq = 0, softirq = 0, steal = 0, guest = 0, guestNice = 0;
    stat >> cpuTag >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal >> guest >> guestNice;
    if (cpuTag != "cpu")
        return 0;

    return user + nice + system + idle + iowait + irq + softirq + steal + guest + guestNice;
}

static std::unordered_map<int, unsigned long long> sampleProcessCpuTimes()
{
    std::unordered_map<int, unsigned long long> samples;
    for (const auto &entry : std::filesystem::directory_iterator("/proc"))
    {
        if (!entry.is_directory())
            continue;

        const std::string pidStr = entry.path().filename().string();
        if (!isNumericPid(pidStr))
            continue;

        int pid = 0;
        try
        {
            pid = std::stoi(pidStr);
        }
        catch (...)
        {
            continue;
        }

        std::ifstream stat(entry.path() / "stat");
        if (!stat)
            continue;

        std::string line;
        std::getline(stat, line);
        const auto rparen = line.rfind(')');
        if (rparen == std::string::npos)
            continue;

        std::string after = line.substr(rparen + 1);
        std::istringstream iss(after);
        char state = 0;
        iss >> state;

        unsigned long long skip = 0;
        for (int i = 0; i < 10; ++i)
            iss >> skip;

        unsigned long long utime = 0, stime = 0;
        iss >> utime >> stime;
        samples.emplace(pid, utime + stime);
    }
    return samples;
}

std::vector<Process> processHistory()
{
    const auto firstProcTimes = sampleProcessCpuTimes();
    const auto firstTotal = readTotalJiffies();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto secondProcTimes = sampleProcessCpuTimes();
    const auto secondTotal = readTotalJiffies();

    const unsigned long long totalDiff =
        secondTotal > firstTotal ? secondTotal - firstTotal : 0;

    std::vector<Process> processes;

    for (const auto &entry : std::filesystem::directory_iterator("/proc"))
    {
        if (!entry.is_directory())
            continue;

        const std::string pidStr = entry.path().filename().string();
        if (!isNumericPid(pidStr))
            continue;

        int pid = 0;
        try
        {
            pid = std::stoi(pidStr);
        }
        catch (...)
        {
            continue;
        }

        std::string name;
        if (!readProcessName(pid, name))
            continue;

        const auto first = firstProcTimes.find(pid);
        const auto second = secondProcTimes.find(pid);
        if (first == firstProcTimes.end() || second == secondProcTimes.end() || second->second < first->second)
            continue;

        float usage = 0.f;
        if (totalDiff > 0)
        {
            const unsigned long long procDiff = second->second - first->second;
            usage = 100.f * static_cast<float>(procDiff) / static_cast<float>(totalDiff);
        }

        processes.push_back({pid, name, usage});
    }

    std::sort(processes.begin(), processes.end(),
              [](const Process &a, const Process &b)
              { return a.cpuUsage > b.cpuUsage; });

    return processes;
}

static float readTempC()
{
    std::ifstream in("/sys/class/hwmon/hwmon4/temp1_input");
    int milli = 0;
    in >> milli;
    return milli / 1000.f;
}

static float readCpuUsage()
{
    std::ifstream stat("/proc/stat");
    if (!stat)
        return 0.f;

    std::string cpuTag;
    unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0;
    unsigned long long irq = 0, softirq = 0, steal = 0, guest = 0, guestNice = 0;
    stat >> cpuTag >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal >> guest >> guestNice;
    if (cpuTag != "cpu")
        return 0.f;

    const unsigned long long idleTime = idle + iowait;
    const unsigned long long nonIdle = user + nice + system + irq + softirq + steal;
    const unsigned long long total = idleTime + nonIdle;

    static unsigned long long prevTotal = 0;
    static unsigned long long prevIdle = 0;
    static bool firstSample = true;

    if (firstSample)
    {
        firstSample = false;
        prevTotal = total;
        prevIdle = idleTime;
        return 0.f;
    }

    const unsigned long long totalDiff = total - prevTotal;
    const unsigned long long idleDiff = idleTime - prevIdle;
    prevTotal = total;
    prevIdle = idleTime;

    if (totalDiff <= 0)
        return 0.f;

    return 100.f * static_cast<float>(totalDiff - idleDiff) / static_cast<float>(totalDiff);
}

static float readRamUsage()
{
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo)
        return 0.f;

    std::string key, unit;
    long value = 0;
    long memTotal = 0;
    long memAvailable = 0;

    while (meminfo >> key >> value >> unit)
    {
        if (key == "MemTotal:")
            memTotal = value;
        else if (key == "MemAvailable:")
        {
            memAvailable = value;
            break;
        }
    }

    if (memTotal == 0)
        return 0.f;

    const long used = memTotal - memAvailable;
    return 100.f * static_cast<float>(used) / static_cast<float>(memTotal);
}

int main(const int argc, const char *argv[])
{
    const sf::Vector2u windowSize{800, 600};
    sf::RenderWindow window(sf::VideoMode(windowSize.x, windowSize.y),
                            "TASK Monitor - usage history",
                            sf::Style::Titlebar | sf::Style::Close);
    window.setFramerateLimit(30);

    const float maxMinutes = 10;
    const float samplePeriod = .5f;
    const std::size_t maxSamples = static_cast<std::size_t>((maxMinutes * 60.f) / samplePeriod);
    std::deque<std::pair<float, float>> history; // (seconds ago, temp)
    const std::array<std::string, 2> tabs = {"CPU", "RAM"};
    const float tabHeight = 40.f;
    const float tabWidth = windowSize.x / static_cast<float>(tabs.size());
    std::size_t activeTab = 0;
    float lastCpuUsage = 0.f;
    float lastRamUsage = 0.f;

    sf::Font font;
    font.loadFromFile("/home/tb07/.local/share/fonts/Poppins-Regular.ttf");

    sf::Clock sampleClock;

    // for CPU page
    sf::RectangleShape btnNext, btnPrev;
    btnNext.setSize({28.f, 28.f});
    btnNext.setPosition(windowSize.x - 60.f, windowSize.y / 2 + 40.0f);
    btnNext.setFillColor(sf::Color(170, 70, 70));
    btnPrev.setSize({28.f, 28.f});
    btnPrev.setPosition(windowSize.x - 100.f, windowSize.y / 2 + 40.0f);
    btnPrev.setFillColor(sf::Color(70, 70, 190));

    unsigned int page = 1;

    window.setFramerateLimit(60);

    sf::View view = window.getDefaultView();

    sf::Text label;
    label.setFont(font);
    label.setCharacterSize(16);
    label.setFillColor(FG_COL);
    std::vector<sf::RectangleShape> tabsRect;
    std::vector<sf::Text> tabsRectText;
    for (std::size_t i = 0; i < tabs.size(); ++i)
    {
        sf::RectangleShape tabRect({tabWidth, tabHeight});
        tabRect.setPosition(i * tabWidth, 0.f);
        tabRect.setFillColor(i == activeTab ? sf::Color(0x1123aeff)
                                            : sf::Color(0x222222aa));

        sf::Text tabText;
        tabText.setFont(font);
        tabText.setCharacterSize(16);
        tabText.setOutlineThickness(1);
        tabText.setOutlineColor(BG_COL);
        tabText.setFillColor(BG_COL);
        tabText.setString(tabs[i]);
        tabText.setPosition((i + 1) * (tabWidth)-tabWidth / 2 - tabText.getString().getSize() / 2 * 16, 10.f);
        tabsRect.push_back(tabRect);
        tabsRectText.push_back(tabText);
    }

    std::vector<float> cpuUsagePoint;

    while (window.isOpen())
    {
        sf::Event evt;
        while (window.pollEvent(evt))
        {
            if (evt.type == sf::Event::Closed || evt.key.code == sf::Keyboard::Escape)
            {
                window.close();
            }
            else if (evt.type == sf::Event::MouseButtonPressed &&
                     evt.mouseButton.button == sf::Mouse::Left &&
                     evt.mouseButton.y <= tabHeight)
            {
                const std::size_t clicked =
                    static_cast<std::size_t>(evt.mouseButton.x / tabWidth);
                if (clicked < tabs.size())
                    activeTab = clicked;
            }
            else if (evt.type == sf::Event::MouseButtonPressed &&
                     evt.mouseButton.button == sf::Mouse::Left)
            {
                const sf::Vector2f mousePos{
                    static_cast<float>(evt.mouseButton.x),
                    static_cast<float>(evt.mouseButton.y)};
                if (btnNext.getGlobalBounds().contains(mousePos))
                {
                    if (page <= processHistory().size() / range)
                        ++page;
                }
                else if (btnPrev.getGlobalBounds().contains(mousePos))
                {
                    if (page > 1)
                        --page;
                }
            }
            else if (evt.type == sf::Event::MouseWheelScrolled)
            {
                float scrollPad = -range;
                view.move(0, scrollPad * evt.mouseWheel.delta);
                std::cout << view.getCenter().y << std::endl;
            }
        }

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Tab))
        {
            activeTab = (activeTab + 1) % tabs.size();
            std::cout << activeTab << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(180));
        }
        else if (tabs.at(activeTab) == tabs[0])
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::LShift) && sf::Keyboard::isKeyPressed(sf::Keyboard::Right))
            {
                sf::sleep(sf::milliseconds(180));
                if (page <= processHistory().size() / range)
                    ++page;
            }
            else if (sf::Keyboard::isKeyPressed(sf::Keyboard::LShift) && sf::Keyboard::isKeyPressed(sf::Keyboard::Left))
            {
                sf::sleep(sf::milliseconds(180));
                if (page > 1)
                    --page;
            }

        if (sampleClock.getElapsedTime().asSeconds() >= samplePeriod)
        {
            sampleClock.restart();
            const float temp = readTempC();
            history.push_back({0.f, temp});
            if (history.size() > maxSamples)
                history.pop_front();
            for (auto &pt : history)
                pt.first += samplePeriod;

            lastCpuUsage = readCpuUsage();
            lastRamUsage = readRamUsage();
        }

        window.clear(BG_COL);

        sf::RectangleShape tabBand({static_cast<float>(windowSize.x), tabHeight});
        tabBand.setFillColor(sf::Color(5, 5, 40));
        window.draw(tabBand);

        for (std::size_t i = 0; i < tabs.size(); ++i)
        {
            auto &tabRect = tabsRect[i];
            tabRect.setFillColor(i == activeTab ? sf::Color(0x1C27AFFF)
                                                : sf::Color(0x111177AA));
            tabsRectText[i].setString(tabs[i]);
            tabsRectText[i].setFillColor(i == activeTab ? FG_COL : BG_COL);
            window.draw(tabRect);
            window.draw(tabsRectText[i]);
        }

        auto formatPercent = [](float value)
        {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << value << " %";
            return oss.str();
        };

        if (activeTab == 0)
        {
            std::vector<Process> procs = processHistory();

            int capped = 0;

            auto formatTemp = [](float value)
            {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(1) << value << " C";
                return oss.str();
            };

            if (!history.empty())
                capped = std::clamp(history.back().second, 0.0f, 100.0f);

            label.setCharacterSize(16);
            label.setString("CPU Usage (" + std::to_string(procs.size()) + " processes; page: " + std::to_string(page) + ", on page: ");
            label.setPosition(40.f, tabHeight + 16.f);

            const float usableWidth = windowSize.x * .85f;
            sf::RectangleShape barBg({usableWidth, 200});
            barBg.setPosition(40.f, tabHeight + 64.f);
            barBg.setFillColor(sf::Color(2, 2, 5, 100));
            barBg.setOutlineThickness(1);
            barBg.setOutlineColor(sf::Color(0x228acfaa));

            sf::VertexArray dots(sf::LineStrip);

            const auto bfg = barBg.getGlobalBounds();
            const auto vs = sf::Vector2f(bfg.left, bfg.top + bfg.height);

            std::vector<sf::Text> procTexts;

            const float procY = vs.y + 10.f;

            int curProcessCount = 0;
            // for (size_t i = (page - 1) * range; i < procs.size() && i < page * range; ++i)
            for (size_t i = 0; i < procs.size(); ++i)
            {
                ++curProcessCount;

                const auto &proc = procs[i];
                std::ostringstream oss;
                oss << i + 1 << ". " << proc.name.substr(0, 15) << " (" << std::fixed
                    << std::setprecision(1) << proc.cpuUsage << " %) - " << proc.pid;

                sf::Text text;
                text.setFont(font);
                text.setString(oss.str());
                text.setCharacterSize(20);
                text.setFillColor(FG_COL);
                text.setPosition(vs.x + 5.0f, procY + 16.5 * i);
                procTexts.push_back(text);
            }
            std::string s = label.getString() + std::to_string(curProcessCount) + ")\nTemp : " + std::to_string(capped) + " C; " + formatPercent(lastCpuUsage);
            cpuUsagePoint.push_back(lastCpuUsage); // push track of cpu usages in 100%
            label.setString(s.c_str());

            if (cpuUsagePoint.size() > barBg.getSize().x / range * 2)
            {
                cpuUsagePoint.erase(cpuUsagePoint.begin(), (cpuUsagePoint.begin() + 2));
            }
            for (int i = cpuUsagePoint.size() - 1; i >= 0; i--)
            {
                sf::Vertex v(sf::Vector2f(barBg.getPosition().x + i * range / 2, barBg.getPosition().y + barBg.getSize().y - cpuUsagePoint.at(i) * 2.0f));
                v.color = sf::Color::Yellow;
                dots.append(v);
            }

            window.setView(view);
            for (auto &&procText : procTexts)
                window.draw(procText);
            window.setView(window.getDefaultView());

            window.draw(barBg);
            window.draw(dots);
            window.draw(label);
            window.draw(btnNext);
            window.draw(btnPrev);
            window.display();
        }
        else
        {
            label.setCharacterSize(20);
            label.setString("RAM Usage");
            label.setPosition(40.f, tabHeight + 20.f);
            window.draw(label);

            sf::Text percentText = label;
            percentText.setCharacterSize(48);
            percentText.setString(formatPercent(lastRamUsage));
            percentText.setPosition(40.f, tabHeight + 60.f);
            window.draw(percentText);

            const float usableWidth = windowSize.x - 80.f;
            sf::RectangleShape barBg({usableWidth, 28.f});
            barBg.setPosition(40.f, tabHeight + 140.f);
            barBg.setFillColor(sf::Color(50, 50, 70));
            window.draw(barBg);

            const float ramRatio =
                std::max(0.f, std::min(100.f, lastRamUsage)) / 100.f;
            sf::RectangleShape barFill({usableWidth * ramRatio, 28.f});
            barFill.setPosition(barBg.getPosition());
            barFill.setFillColor(sf::Color(200, 160, 40));
            window.draw(barFill);
            window.display();
        }
    }
    return 0;
}