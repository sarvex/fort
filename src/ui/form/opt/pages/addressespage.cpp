#include "addressespage.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabBar>
#include <QVBoxLayout>

#include "../../../conf/addressgroup.h"
#include "../../../conf/firewallconf.h"
#include "../../../fortmanager.h"
#include "../../../fortsettings.h"
#include "../../../util/net/netutil.h"
#include "../controls/textarea2splitter.h"
#include "../controls/textarea2splitterhandle.h"
#include "../optionscontroller.h"
#include "addresses/addressescolumn.h"

AddressesPage::AddressesPage(OptionsController *ctrl,
                             QWidget *parent) :
    BasePage(ctrl, parent)
{
    setupUi();
}

void AddressesPage::onRetranslateUi()
{
    m_tabBar->setTabText(0, tr("Internet Addresses"));
    m_tabBar->setTabText(1, tr("Allowed Internet Addresses"));

    m_includeAddresses->labelTitle()->setText(tr("Include"));
    m_includeAddresses->cbUseAll()->setText(tr("Include All"));

    m_excludeAddresses->labelTitle()->setText(tr("Exclude"));
    m_excludeAddresses->cbUseAll()->setText(tr("Exclude All"));

    m_splitter->handle()->btMoveAllFrom1To2()->setToolTip(tr("Move All Lines to 'Exclude'"));
    m_splitter->handle()->btMoveAllFrom2To1()->setToolTip(tr("Move All Lines to 'Include'"));
    m_splitter->handle()->btMoveSelectedFrom1To2()->setToolTip(tr("Move Selected Lines to 'Exclude'"));
    m_splitter->handle()->btMoveSelectedFrom2To1()->setToolTip(tr("Move Selected Lines to 'Include'"));

    retranslateAddressesPlaceholderText();
}

void AddressesPage::setupUi()
{
    auto layout = new QVBoxLayout();

    // Tab Bar
    m_tabBar = new QTabBar();
    layout->addWidget(m_tabBar);

    m_tabBar->addTab(QIcon(":/images/world.png"), QString());
    m_tabBar->addTab(QIcon(":/images/world_link.png"), QString());

    // Address Columns
    setupIncludeAddresses();
    setupExcludeAddresses();

    setupAddressesUseAllEnabled();

    setupAddressGroup();

    // Splitter
    setupSplitter();
    layout->addWidget(m_splitter, 1);

    this->setLayout(layout);
}

void AddressesPage::setupSplitter()
{
    m_splitter = new TextArea2Splitter(ctrl());

    m_splitter->setSettingsPropName("optWindowAddrSplit");

    Q_ASSERT(!m_splitter->handle());

    m_splitter->addWidget(m_includeAddresses);
    m_splitter->addWidget(m_excludeAddresses);

    Q_ASSERT(m_splitter->handle());

    m_splitter->handle()->setTextArea1(m_includeAddresses->editIpText());
    m_splitter->handle()->setTextArea2(m_excludeAddresses->editIpText());
}

void AddressesPage::setupIncludeAddresses()
{
    m_includeAddresses = new AddressesColumn();

    connect(m_includeAddresses->cbUseAll(), &QCheckBox::toggled, [&](bool checked) {
        if (addressGroup()->includeAll() == checked)
            return;

        addressGroup()->setIncludeAll(checked);

        ctrl()->setConfFlagsEdited(true);
    });
    connect(m_includeAddresses->editIpText(), &QPlainTextEdit::textChanged, [&] {
        const auto ipText = m_includeAddresses->editIpText()->toPlainText();

        if (addressGroup()->includeText() == ipText)
            return;

        addressGroup()->setIncludeText(ipText);

        ctrl()->setConfEdited(true);
    });
}

void AddressesPage::setupExcludeAddresses()
{
    m_excludeAddresses = new AddressesColumn();

    connect(m_excludeAddresses->cbUseAll(), &QCheckBox::toggled, [&](bool checked) {
        if (addressGroup()->excludeAll() == checked)
            return;

        addressGroup()->setExcludeAll(checked);

        ctrl()->setConfFlagsEdited(true);
    });
    connect(m_excludeAddresses->editIpText(), &QPlainTextEdit::textChanged, [&] {
        const auto ipText = m_excludeAddresses->editIpText()->toPlainText();

        if (addressGroup()->excludeText() == ipText)
            return;

        addressGroup()->setExcludeText(ipText);

        ctrl()->setConfEdited(true);
    });
}

void AddressesPage::setupAddressesUseAllEnabled()
{
    const auto refreshUseAllEnabled = [&] {
        auto cbIncludeAll = m_includeAddresses->cbUseAll();
        auto cbExcludeAll = m_excludeAddresses->cbUseAll();

        const bool includeAll = cbIncludeAll->isChecked();
        const bool excludeAll = cbExcludeAll->isChecked();

        cbIncludeAll->setEnabled(includeAll || !excludeAll);
        cbExcludeAll->setEnabled(!includeAll || excludeAll);
    };

    refreshUseAllEnabled();

    connect(m_includeAddresses->cbUseAll(), &QCheckBox::toggled, this, refreshUseAllEnabled);
    connect(m_excludeAddresses->cbUseAll(), &QCheckBox::toggled, this, refreshUseAllEnabled);
}

void AddressesPage::retranslateAddressesPlaceholderText()
{
    const auto placeholderText = tr("# Examples:") + '\n'
            + NetUtil::localIpv4Networks().join('\n');

    m_includeAddresses->editIpText()->setPlaceholderText(placeholderText);
    m_excludeAddresses->editIpText()->setPlaceholderText(placeholderText);
}

void AddressesPage::setupAddressGroup()
{
    const auto refreshAddressGroup = [&] {
        const int tabIndex = m_tabBar->currentIndex();
        m_addressGroup = conf()->addressGroupsList().at(tabIndex);

        m_includeAddresses->cbUseAll()->setChecked(addressGroup()->includeAll());
        m_includeAddresses->editIpText()->setPlainText(addressGroup()->includeText());

        m_excludeAddresses->cbUseAll()->setChecked(addressGroup()->excludeAll());
        m_excludeAddresses->editIpText()->setPlainText(addressGroup()->excludeText());
    };

    refreshAddressGroup();

    connect(m_tabBar, &QTabBar::currentChanged, this, refreshAddressGroup);
}