#include <QMessageBox>
#include <QStandardItemModel>
#include <set>
#include <vector>
#include <algorithm>
#include "RaidCalc.h"
#include "PokemonNames.hpp"
#include "Benchmarks.h"

RaidCalc::RaidCalc(QWidget* parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);
    if (!SeedFinder::initialize())
    {
        QMessageBox::critical(this, "Error", "Failed to initialize seed finder.");
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    }
    std::set<uint32_t> encounterable_species;
    finder.visit_encounters([&](const EncounterTera9& enc) { encounterable_species.insert(enc.species); });
    std::vector<std::pair<std::string, uint32_t>> species_data;
    for (auto& species : encounterable_species)
        species_data.push_back({ pokemon_names[species], species });
    std::sort(species_data.begin(), species_data.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    ui.comboBoxSpecies->addItem("Any", 0U);
    for (auto& pair : species_data)
        ui.comboBoxSpecies->addItem(pair.first.c_str(), pair.second);
    itemFilters = new ItemFilterDialog(this);
    seedViewer = new SeedViewerDialog(this);
    ui.tableSeeds->setModel(&seedModel);
    finder_timer = new QTimer(this);
    connect(finder_timer, &QTimer::timeout, this, &RaidCalc::on_finder_timer_timeout);
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    for (uint32_t i = 0; i < sys_info.dwNumberOfProcessors - 1; ++i)
        ui.comboBoxThreads->addItem(QString::number(i + 1));
    ui.comboBoxThreads->setCurrentIndex(ui.comboBoxThreads->count() - 1);

    min_iv_widgets[0] = ui.spinBoxMinHP;
    min_iv_widgets[1] = ui.spinBoxMinAtk;
    min_iv_widgets[2] = ui.spinBoxMinDef;
    min_iv_widgets[3] = ui.spinBoxMinSpA;
    min_iv_widgets[4] = ui.spinBoxMinSpD;
    min_iv_widgets[5] = ui.spinBoxMinSpe;
    max_iv_widgets[0] = ui.spinBoxMaxHP;
    max_iv_widgets[1] = ui.spinBoxMaxAtk;
    max_iv_widgets[2] = ui.spinBoxMaxDef;
    max_iv_widgets[3] = ui.spinBoxMaxSpA;
    max_iv_widgets[4] = ui.spinBoxMaxSpD;
    max_iv_widgets[5] = ui.spinBoxMaxSpe;

    // TODO: Implement filters
    ui.labelTeraType->setVisible(false);
    ui.labelAbility->setVisible(false);
    ui.labelNature->setVisible(false);
    ui.labelGender->setVisible(false);
    ui.comboBoxTeraType->setVisible(false);
    ui.comboBoxAbility->setVisible(false);
    ui.comboBoxNature->setVisible(false);
    ui.comboBoxGender->setVisible(false);

    do_benchmarks(finder);
}

RaidCalc::~RaidCalc()
{

}

QStandardItem* RaidCalc::readonly_item(QString text)
{
    QStandardItem* item = new QStandardItem(text);
    item->setEditable(false);
    return item;
}

void RaidCalc::toggle_ui(bool enabled)
{
    for (auto widget : ui.basicGroup->findChildren<QWidget*>())
        widget->setEnabled(enabled);
    for (auto widget : ui.pokemonGroup->findChildren<QWidget*>())
        widget->setEnabled(enabled);
    for (auto widget : ui.itemGroup->findChildren<QWidget*>())
        widget->setEnabled(enabled);
    ui.tableSeeds->setEnabled(enabled);
    ui.actionSeedViewer->setEnabled(enabled);
    if (!enabled)
    {
        itemFilters->hide();
        seedViewer->hide();
        seedModel.clear();
    }
}

void RaidCalc::on_buttonFindSeeds_clicked()
{
    finder.game = (Game)ui.comboBoxGame->currentIndex();
    finder.stars = ui.comboBoxStars->currentIndex() + 1;
    finder.thread_count = ui.comboBoxThreads->currentIndex() + 1;
    if (!hex_to_uint32(ui.editMinSeed->text(), finder.min_seed))
    {
        QMessageBox::critical(this, "Error", "Bad min seed value.");
        return;
    }
    if (!hex_to_uint32(ui.editMaxSeed->text(), finder.max_seed))
    {
        QMessageBox::critical(this, "Error", "Bad max seed value.");
        return;
    }
    if (finder.max_seed < finder.min_seed)
    {
        QMessageBox::critical(this, "Error", "Max seed cannot be smaller than min seed.");
        return;
    }
    finder.species = ui.comboBoxSpecies->currentData().toUInt();
    finder.shiny = ui.comboBoxShiny->currentIndex();
    finder.tera_type = ui.comboBoxTeraType->currentIndex();
    finder.ability = ui.comboBoxAbility->currentIndex();
    finder.nature = ui.comboBoxNature->currentIndex();
    finder.gender = ui.comboBoxGender->currentIndex();
    for (size_t i = 0; i < _countof(min_iv_widgets); ++i)
        finder.min_iv[i] = min_iv_widgets[i]->value();
    for (size_t i = 0; i < _countof(max_iv_widgets); ++i)
        finder.max_iv[i] = max_iv_widgets[i]->value();
    finder.item_filters_active = ui.checkboxItemFilters->isChecked();
    finder.drop_threshold = ui.spinBoxMinItemsSum->value();
    itemFilters->update_seed_finder(finder);
    if (!finder.use_filters())
    {
        uint64_t seed_count = finder.max_seed - finder.min_seed + 1ULL;
        if (seed_count > MaxSeeds)
        {
            QMessageBox::critical(this, "Error", QString("No filters set. Search aborted because it would exceed maximum allowed number of results (%1).").arg(MaxSeeds));
            return;
        }
        if (seed_count > SeedCountWarningThreshold)
        {
            auto result = QMessageBox::question(this, "Warning", "No filters set, this may result in a huge result set. Do you want to continue anyway?");
            if (result != QMessageBox::Yes)
                return;
        }
    }
    toggle_ui(false);
    if (!finder.find_seeds())
    {
        QMessageBox::critical(this, "Error", "Failed to start seed finder.");
        toggle_ui(true);
        return;
    }
    finder_timer->start(250);
}

void RaidCalc::on_buttonEditFilters_clicked()
{
    itemFilters->show();
}

void RaidCalc::on_buttonResetPokemonFilters_clicked()
{
    ui.comboBoxSpecies->setCurrentIndex(0);
    ui.comboBoxShiny->setCurrentIndex(0);
    for (auto& widget : min_iv_widgets)
        widget->setValue(0);
    for (auto& widget : max_iv_widgets)
        widget->setValue(31);
}

void RaidCalc::on_buttonMaxIV_clicked()
{
    for (auto& widget : min_iv_widgets)
        widget->setValue(31);
    for (auto& widget : max_iv_widgets)
        widget->setValue(31);
}

void RaidCalc::on_finder_timer_timeout()
{
    if (!finder.is_search_done())
        return;
    finder_timer->stop();
    QString msg = QString("Found %1 seeds in %2ms.").arg(QString::number(finder.seeds.size()), QString::number(finder.time_taken.milliseconds()));
    QMessageBox::information(this, "Success", msg);
    seedModel.populateModel(finder);
    resultGame = finder.game;
    resultStars = finder.stars;
    toggle_ui(true);
}

void RaidCalc::on_actionSeedViewer_triggered(bool checked)
{
    seedViewer->show();
}

void RaidCalc::on_tableSeeds_doubleClicked(const QModelIndex& index)
{
    seedViewer->display_seed(resultGame, resultStars, seedModel.get_seed(index.row()));
    seedViewer->show();
}