#include <Arduino.h>
#include "outline_map.h"
#include "outline_data.h"

void drawOutlineMap(lgfx::LovyanGFX& display, const MapProjection::View& view)
{
    double north, west, south, east;
    view.fromScreen(0, 15, north, west);
    view.fromScreen(239, 123, south, east);
    int firstRow = constrain(int(floor((south + 90) / 10)), 0, 17);
    int lastRow = constrain(int(floor((north + 90) / 10)), 0, 17);
    int firstCol = int(floor((west + 180) / 10));
    int lastCol = int(floor((east + 180) / 10));
    if (lastCol < firstCol) lastCol += 36;
    lastCol = min(lastCol, firstCol + 35);
    display.setClipRect(0, 15, 240, 109);
    for (int row = firstRow; row <= lastRow; ++row)
        for (int column = firstCol; column <= lastCol; ++column)
        {
            int col = column % 36;
            double baseLon = col * 10 - 180, baseLat = row * 10 - 90;
            const auto& cell = OUTLINE_CELLS[row * 36 + col];
            for (uint32_t i = cell.segmentStart; i < cell.segmentStart + cell.segmentCount; ++i)
            {
                const auto& segment = OUTLINE_SEGMENTS[i];
                int x1, y1, x2, y2;
                view.toScreen(baseLat + segment.lat1 / 1000.0, baseLon + segment.lon1 / 1000.0, x1, y1);
                view.toScreen(baseLat + segment.lat2 / 1000.0, baseLon + segment.lon2 / 1000.0, x2, y2);
                if ((x1 < 0 && x2 < 0) || (x1 >= 240 && x2 >= 240) ||
                    (y1 < 15 && y2 < 15) || (y1 >= 124 && y2 >= 124)) continue;
                display.drawLine(x1, y1, x2, y2, 0x7BEF);
            }
        }
    struct Box { int x, y, width; } labels[20];
    int labelCount = 0;
    display.setTextSize(1);
    display.setTextColor(0xAD55, BLACK);
    for (int row = firstRow; row <= lastRow; ++row)
        for (int column = firstCol; column <= lastCol; ++column)
        {
            int col = column % 36;
            const auto& cell = OUTLINE_CELLS[row * 36 + col];
            for (uint32_t i = cell.townStart; i < cell.townStart + cell.townCount; ++i)
            {
                const auto& town = OUTLINE_TOWNS[i];
                int x, y;
                view.toScreen(row * 10 - 90 + town.lat / 1000.0,
                              col * 10 - 180 + town.lon / 1000.0, x, y);
                if (x < 2 || x > 237 || y < 22 || y > 115) continue;
                display.fillCircle(x, y, 1, 0xAD55);
                int width = display.textWidth(town.name);
                int labelX = constrain(x + 3, 1, max(1, 239 - width)), labelY = y - 9;
                bool overlap = false;
                for (int j = 0; j < labelCount && !overlap; ++j)
                    overlap = labelX < labels[j].x + labels[j].width + 3 &&
                              labelX + width + 3 > labels[j].x && abs(labelY - labels[j].y) < 10;
                if (overlap || labelCount >= 20) continue;
                labels[labelCount++] = {labelX, labelY, width};
                display.setCursor(labelX, labelY);
                display.print(town.name);
            }
        }
    display.clearClipRect();
}
