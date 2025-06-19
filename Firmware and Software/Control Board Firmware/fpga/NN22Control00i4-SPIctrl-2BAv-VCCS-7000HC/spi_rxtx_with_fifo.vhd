-- Firmware for NN22_ControlBoard00
-- SPI transceiver with FIFO
-- FPGA is the slave device
-- Assuming SPI mode 0

-- Initial version: 2023-3-13
-- Bernhard Zimmermann - bzim@bu.edu
-- Boston University Neurophotonics Center

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity spi_rxtx_with_fifo is
	port (
		ClkxCI 	 		: in std_logic;
		ResetxRI 		: in std_logic;
		ResetTxFIFOxSI 	: in std_logic;
		CSnxSI			: in std_logic;
		MISOxDO 		: out std_logic;
		MOSIxDI			: in std_logic;
		SCKxSI			: in std_logic;
		BuffFullxSO		: out std_logic;
		BuffEmptyxSO	: out std_logic;
		ParDatRdyxSI 	: in std_logic;
		ParDatxDI 		: in std_logic_vector(7 downto 0);
		ParDatxDO		: out std_logic_vector(7 downto 0);
		ParDatRdyxSO 	: out std_logic;
		RxIndxSO		: out std_logic;
		TxIndxSO		: out std_logic
	);
end spi_rxtx_with_fifo;

architecture behavioral of spi_rxtx_with_fifo is

	constant FIFO_ALMOST_EMPTY_TRSH : integer := 6144;
	
	type fsmstatetype is (sIdle, sFirstByte, sFirstByteRdy, sRxData, sRxDataRdy, sTxBytesAvailableHighByte, sTxData, sRstFIFO);
	signal StatexDP, StatexDN : fsmstatetype;
	
	signal FIFOWrAddrxDP, FIFOWrAddrxDN : unsigned(12 downto 0);
	signal FIFORdAddrxDP, FIFORdAddrxDN : unsigned(12 downto 0);
	
	signal ResetTxFIFOxSP, ResetTxFIFOxSN : std_logic;
	signal ResetTxFIFORqdxSP, ResetTxFIFORqdxSN : std_logic;
	signal FIFOWrAddrRstxS : std_logic;
	
	signal FIFOBytesAvailableFullxD : unsigned(FIFORdAddrxDP'range);
	signal FIFOBytesAvailableHighBytexDP, FIFOBytesAvailableHighBytexDN : std_logic_vector(7 downto 0);
	signal FIFOFullxSP, FIFOFullxSN : std_logic; 
	signal FIFOEmptyxS : std_logic;
	signal FIFOAlmostEmptyxSP, FIFOAlmostEmptyxSN : std_logic;
	signal FIFOOutDataxD : std_logic_vector(7 downto 0);
	signal TxSRegxDP, TxSRegxDN : std_logic_vector(7 downto 0);
	signal RxSRegxDP, RxSRegxDN : std_logic_vector(7 downto 0);
	
	signal BitCntxDP, BitCntxDN: integer range 0 to 7;
	
	-- synchronizer registers
	signal CSnxSP, CSnxSN : std_logic;
	signal SCKSRegxSP, SCKSRegxSN : std_logic_vector(1 downto 0);
	signal MOSIRegxDP, MOSIRegxDN : std_logic;
	
	signal DUMMY_A : std_logic_vector(7 downto 0);
	
	
	component spi_txrx_ram
    port (
		DataInA	: in  std_logic_vector(7 downto 0); 
        DataInB	: in  std_logic_vector(7 downto 0); 
        AddressA: in  std_logic_vector(12 downto 0); 
        AddressB: in  std_logic_vector(12 downto 0); 
        ClockA	: in  std_logic; ClockB: in  std_logic; 
        ClockEnA: in  std_logic; ClockEnB: in  std_logic; 
        WrA		: in  std_logic; WrB: in  std_logic; ResetA: in  std_logic; 
        ResetB	: in  std_logic; QA: out  std_logic_vector(7 downto 0); 
        QB		: out std_logic_vector(7 downto 0)
	);
	end component;
	
begin
	
	FIFOEmptyxS <= '1' when FIFORdAddrxDP = FIFOWrAddrxDP else '0';
	FIFOAlmostEmptyxSN <= '1' when FIFOBytesAvailableFullxD < FIFO_ALMOST_EMPTY_TRSH else '0';
	BuffEmptyxSO <= FIFOAlmostEmptyxSP;
	FIFOFullxSN <= '1' when (FIFOWrAddrxDP+1+3) = FIFORdAddrxDP else '0'; -- +1 not using last mem loc to simplify, +3 spare
	BuffFullxSO <= FIFOFullxSP;
	-- no safeguard against overwriting last mem location (if FIFO is allowed to fill up, we'll have discontinuities
	-- in the datastream anyhow)
	FIFOWrAddrxDN <= (others => '0') when FIFOWrAddrRstxS = '1' else
					FIFOWrAddrxDP when (ParDatRdyxSI = '0' or FIFOFullxSP = '1') else
					FIFOWrAddrxDP +1;
	FIFOBytesAvailableFullxD <= FIFOWrAddrxDP - FIFORdAddrxDP;
	
	-- FIFO reset logic related
	ResetTxFIFOxSN <= ResetTxFIFOxSI;
	ResetTxFIFORqdxSN <= '1' when ResetTxFIFOxSP = '0' and ResetTxFIFOxSI = '1' else -- rising edge
						 '0' when FIFOWrAddrRstxS = '1' else
						 ResetTxFIFORqdxSP;
	
	-- == non-linear mapping of bytes available ==
	-- MSB represents >=1024 bytes available in the buffer
	-- bits 6 downto 0 are in normal linear mapping
	-- FIFOBytesAvailableLtdxD(7) <= FIFOBytesAvailableFullxD(FIFOBytesAvailableFullxD'high);
	-- FIFOBytesAvailableLtdxD(6 downto 0) <= FIFOBytesAvailableFullxD(6 downto 0) when FIFOBytesAvailableFullxD(FIFOBytesAvailableFullxD'high-1 downto 0) < 128 
	--										else (others => '1');
	-- == linear mapping ==
	-- FIFOBytesAvailableLtdxD <= FIFOBytesAvailableFullxD(7 downto 0) when FIFOBytesAvailableFullxD < 256 else (others => '1');
	
	CSnxSN <= CSnxSI;
	SCKSRegxSN(0) <= SCKxSI;
	SCKSRegxSN(1) <= SCKSRegxSP(0);
	MOSIRegxDN <= MOSIxDI;
	ParDatxDO <= RxSRegxDP;
	
	p_memzing : process (ClkxCI, ResetxRI)
	begin
		if (ResetxRI = '1') then
			StatexDP <= sIdle;
			FIFOWrAddrxDP <= (others => '0');
			FIFORdAddrxDP <= (others => '0');
			FIFOAlmostEmptyxSP <= '0';
			FIFOFullxSP <= '0';
			ResetTxFIFOxSP <= '0';
			ResetTxFIFORqdxSP <= '0';
			TxSRegxDP  <= (others => '0');
			RxSRegxDP  <= (others => '0');
			FIFOBytesAvailableHighBytexDP <= (others => '0');
			CSnxSP <= '1';
			SCKSRegxSP <= (others => '0');
			BitCntxDP <= 0;
			MOSIRegxDP <= '0';
			
		elsif (rising_edge(ClkxCI)) then
			StatexDP <= StatexDN;
			FIFOWrAddrxDP <= FIFOWrAddrxDN;
			FIFORdAddrxDP <= FIFORdAddrxDN;
			FIFOAlmostEmptyxSP <= FIFOAlmostEmptyxSN;
			FIFOFullxSP <= FIFOFullxSN;
			ResetTxFIFOxSP <= ResetTxFIFOxSN;
			ResetTxFIFORqdxSP <= ResetTxFIFORqdxSN;
			TxSRegxDP <= TxSRegxDN;
			RxSRegxDP <= RxSRegxDN;
			FIFOBytesAvailableHighBytexDP <= FIFOBytesAvailableHighBytexDN;
			CSnxSP <= CSnxSN;
			SCKSRegxSP <= SCKSRegxSN;
			BitCntxDP <= BitCntxDN;
			MOSIRegxDP <= MOSIRegxDN;			
		end if;
	end process;
	
	p_memless : process (StatexDP, CSnxSP, ResetTxFIFORqdxSP, SCKSRegxSP, BitCntxDP, FIFORdAddrxDP, FIFOEmptyxS, 
						FIFOBytesAvailableFullxD, FIFOOutDataxD, TxSRegxDP, RxSRegxDP, FIFOBytesAvailableHighBytexDP, MOSIRegxDP)
	begin
		MISOxDO <= 'Z';
		TxSRegxDN <= TxSRegxDP;
		RxSRegxDN <= RxSRegxDP;
		BitCntxDN <= BitCntxDP;
		FIFOBytesAvailableHighBytexDN <= FIFOBytesAvailableHighBytexDP;
		FIFOWrAddrRstxS <= '0';
		FIFORdAddrxDN <= FIFORdAddrxDP;
		ParDatRdyxSO <= '0';
		RxIndxSO <= '0';
		TxIndxSO <= '0';
		StatexDN <= StatexDP;
		case StatexDP is 
			when sIdle =>
				TxSRegxDN <= std_logic_vector(FIFOBytesAvailableFullxD(7 downto 0));
				FIFOBytesAvailableHighBytexDN((FIFOBytesAvailableFullxD'left-8) downto 0) <= std_logic_vector(FIFOBytesAvailableFullxD(FIFOBytesAvailableFullxD'left downto 8));
				FIFOBytesAvailableHighBytexDN(7 downto (FIFOBytesAvailableFullxD'left-8+1)) <= (others => '0');
				BitCntxDN <= 0;
				if ResetTxFIFORqdxSP = '1' then
					StatexDN <= sRstFIFO;
				elsif CSnxSP = '0' then
					StatexDN <= sFirstByte;
				end if;

			-- first byte determines whether we transmit or receive data
			when sFirstByte =>
				MISOxDO <= TxSRegxDP(7);
				if CSnxSP = '1' then
					StatexDN <= sIdle;
				elsif SCKSRegxSP = "01" then -- rising edge
					RxSRegxDN <= std_logic_vector(shift_left(unsigned(RxSRegxDP), 1));
					RxSRegxDN(0) <= MOSIRegxDP;
				elsif SCKSRegxSP = "10" then -- falling edge
					if BitCntxDP >= 7 then
					-- last edge in byte is falling, so make state transition here
						BitCntxDN <= 0;
						if RxSRegxDP = X"00" then -- tx data
							TxSRegxDN <= FIFOBytesAvailableHighBytexDP;
							StatexDN <= sTxBytesAvailableHighByte;
						else -- receive command
							StatexDN <= sFirstByteRdy;
						end if;			
					else -- shift out
						TxSRegxDN <= std_logic_vector(shift_left(unsigned(TxSRegxDP), 1));
						BitCntxDN <= BitCntxDP +1;
					end if;
				end if;
			
			-- States related to receiving data/commands from Raspberry Pi
			when sFirstByteRdy =>
				ParDatRdyxSO <= '1';
				StatexDN <= sRxData;
			when sRxData =>
				MISOxDO <= '0';
				RxIndxSO <= '1';
				if CSnxSP = '1' then
					StatexDN <= sIdle;
				elsif SCKSRegxSP = "01" then -- rising edge
					RxSRegxDN <= std_logic_vector(shift_left(unsigned(RxSRegxDP), 1));
					RxSRegxDN(0) <= MOSIRegxDP;
				elsif SCKSRegxSP = "10" then -- falling edge
					if BitCntxDP >= 7 then
						StatexDN <= sRxDataRdy;
					else
						BitCntxDN <= BitCntxDP +1;
					end if;
				end if;	
			when sRxDataRdy =>
				BitCntxDN <= 0;
				ParDatRdyxSO <= '1';
				StatexDN <= sRxData;
			
			-- States related to transmitting data to Raspberry Pi
			when sTxBytesAvailableHighByte =>
				MISOxDO <= TxSRegxDP(7);
				if CSnxSP = '1' then
					StatexDN <= sIdle;
				elsif SCKSRegxSP = "10" then -- falling edge
					if BitCntxDP >= 7 then
					-- last edge in byte is falling, so make state transition here
						BitCntxDN <= 0;
						TxSRegxDN <= FIFOOutDataxD;
						StatexDN <= sTxData;
					else -- shift out
						TxSRegxDN <= std_logic_vector(shift_left(unsigned(TxSRegxDP), 1));
						BitCntxDN <= BitCntxDP +1;
					end if;
				end if;
			when sTxData =>
				MISOxDO <= TxSRegxDP(7);
				if CSnxSP = '1' then
					StatexDN <= sIdle;
				elsif SCKSRegxSP = "10" then -- falling edge
					if BitCntxDP >= 7 then
						TxSRegxDN <= FIFOOutDataxD;
						BitCntxDN <= 0;
						TxIndxSO <= '1';
					else -- shift out
						TxSRegxDN <= std_logic_vector(shift_left(unsigned(TxSRegxDP), 1));
						BitCntxDN <= BitCntxDP +1;
					end if;
					-- current addr has been loaded in SReg
					-- prepare next data
					if BitCntxDP = 1 and FIFOEmptyxS = '0' then
						FIFORdAddrxDN <= FIFORdAddrxDP +1;
					end if;
				end if;
				
			-- Misc states
			when sRstFIFO =>
				FIFOWrAddrRstxS <= '1';
				FIFORdAddrxDN <= (others => '0');
				StatexDN <= sIdle;
			when others =>
				StatexDN <= sIdle;
		end case;
	end process;


	-- component instances
	spi_txrx_ram_inst : spi_txrx_ram
    port map (
		DataInA => ParDatxDI, 
		DataInB => (others => '0'), 
		AddressA => std_logic_vector(FIFOWrAddrxDP), 
        AddressB => std_logic_vector(FIFORdAddrxDP), 
		ClockA => ClkxCI, 
		ClockB => ClkxCI, 
		ClockEnA => '1', 
        ClockEnB => '1', 
		WrA => ParDatRdyxSI, 
		WrB => '0', 
		ResetA => ResetxRI, 
		ResetB => ResetxRI, 
		QA(7 downto 0) => DUMMY_A, 
        QB(7 downto 0) => FIFOOutDataxD
	);

end behavioral;