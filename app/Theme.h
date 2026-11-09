#pragma once
#include <QString>

namespace Theme {
    const QString WindowBg = "background-color: #0f0f0f; color: white; font-family: sans-serif;";
    const QString PanelBg = "background-color: #1a1a1a; color: #555; border-left: 1px solid #333; outline: none;";
    const QString PlaylistItem = "QListWidget::item { padding: 15px 10px; border-bottom: 1px solid #222; } QListWidget::item:hover { background-color: #2a2a2a; color: #888; } QListWidget::item:selected { background-color: #1E90FF; color: white; font-weight: bold; }";
    const QString Button = "QPushButton { background-color: #333; border-radius: 4px; padding: 6px 10px; font-weight: bold; border: none; color: white; } QPushButton:hover { background-color: #555; }";
    const QString SliderGroove = "background: #333; height: 6px; border-radius: 3px;";
    const QString SliderHandle = "background: white; width: 12px; margin-top: -3px; margin-bottom: -3px; border-radius: 6px;";
    const QString SeekColor = "background: #1E90FF; border-radius: 3px;";
    const QString VolColor = "background: #4CAF50; border-radius: 3px;";
}